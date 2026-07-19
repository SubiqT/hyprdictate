#include "socket_client.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#include <hyprland/src/Compositor.hpp>

#include <wayland-server-core.h>

#include "log.hpp"

namespace hyprdictate {

    namespace fs = std::filesystem;

    namespace {

        fs::path defaultSocketPath() {
            if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg)
                return fs::path{xdg} / "hyprdictate.sock";
            return fs::path{"/tmp"} / "hyprdictate.sock";
        }

    }

    SocketClient::SocketClient(Callbacks callbacks)
        : m_callbacks(std::move(callbacks))
    {}

    SocketClient::~SocketClient() {
        // Order matters: remove the event source before closing the
        // fd so the compositor doesn't fire onFdReady on a stale fd
        // between the close() and the wl_event_source_remove().
        if (m_retryTimer) {
            wl_event_source_remove(m_retryTimer);
            m_retryTimer = nullptr;
        }
        if (m_source) {
            wl_event_source_remove(m_source);
            m_source = nullptr;
        }
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

    bool SocketClient::connect() {
        const auto path = defaultSocketPath();

        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            log::warn("socket_client: socket(): {}", std::strerror(errno));
            scheduleReconnect();
            return false;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const auto path_str = path.string();
        if (path_str.size() >= sizeof(addr.sun_path)) {
            log::warn("socket_client: socket path too long: {}", path_str);
            ::close(fd);
            // Path length is a config error, not a transient one;
            // don't schedule a retry — it would just fail identically.
            return false;
        }
        std::memcpy(addr.sun_path, path_str.data(), path_str.size());

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            log::warn("socket_client: connect({}): {} (retrying in {}ms)",
                      path_str, std::strerror(errno), m_backoffMs);
            ::close(fd);
            scheduleReconnect();
            return false;
        }

        // Hyprland's wl_event_loop is available via the compositor
        // singleton (see /tmp/hyprland/src/Compositor.hpp:33). Adding
        // our fd here means every daemon event fires as a callback
        // on the compositor's own main thread — same execution
        // context Hyprland's own event handlers run in, so plugin
        // state doesn't need any locking.
        if (!g_pCompositor || !g_pCompositor->m_wlEventLoop) {
            log::warn("socket_client: compositor wl_event_loop not available yet");
            ::close(fd);
            scheduleReconnect();
            return false;
        }

        m_fd     = fd;
        m_source = wl_event_loop_add_fd(
            g_pCompositor->m_wlEventLoop,
            fd,
            WL_EVENT_READABLE,
            &SocketClient::onFdReady,
            this);

        if (!m_source) {
            log::warn("socket_client: wl_event_loop_add_fd failed");
            ::close(m_fd);
            m_fd = -1;
            scheduleReconnect();
            return false;
        }

        log::info("socket_client: connected to {}", path_str);

        // Send the identify handshake immediately. The daemon logs it
        // (and, from M2.8, uses it to gate the wtype fallback path).
        // A failure here is treated as a soft error: connect is still
        // successful and dispatchers work, but the daemon may double-
        // inject with wtype. Log and move on.
        if (!send(command::Identify{ .role = "plugin" })) {
            log::warn("socket_client: identify write failed; wtype suppression "
                      "may not activate on the daemon side");
        }

        // Successful connect + identify: reset the backoff so a
        // future disconnect starts retrying from the short end
        // again.
        m_backoffMs = kBackoffInitMs;

        return true;
    }

    bool SocketClient::send(const Command& cmd) {
        if (m_fd < 0) {
            log::warn("socket_client: send() called while disconnected");
            return false;
        }

        const auto payload = serialize(cmd).dump() + "\n";

        std::size_t written = 0;
        while (written < payload.size()) {
            const ssize_t n = ::write(m_fd, payload.data() + written,
                                      payload.size() - written);
            if (n < 0) {
                if (errno == EINTR) continue;
                log::warn("socket_client: write(): {}", std::strerror(errno));
                handleDisconnect("write failed");
                return false;
            }
            written += static_cast<std::size_t>(n);
        }
        return true;
    }

    int SocketClient::onFdReady(int /*fd*/, uint32_t mask, void* userdata) {
        auto* self = static_cast<SocketClient*>(userdata);

        // Handle error / hangup first so a downstream read doesn't
        // spin on an already-dead socket.
        if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
            self->handleDisconnect(
                (mask & WL_EVENT_HANGUP) ? "peer hangup" : "socket error");
            return 0;
        }

        if (mask & WL_EVENT_READABLE)
            self->handleReadable();

        // Return value is the number of "outstanding events" per
        // wayland-server API; returning 0 is standard for handlers
        // that don't dispatch further events.
        return 0;
    }

    void SocketClient::handleReadable() {
        // Read whatever's available in a single read(). The daemon
        // writes small JSON lines (rarely more than a few hundred
        // bytes), so 4 KiB per read comfortably covers the common
        // case without needing a loop until EAGAIN.
        char buf[4096];
        const ssize_t n = ::read(m_fd, buf, sizeof(buf));
        if (n == 0) {
            handleDisconnect("EOF from daemon");
            return;
        }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                return;
            handleDisconnect(std::strerror(errno));
            return;
        }

        m_readbuf.append(buf, static_cast<std::size_t>(n));

        // Split on '\n' and dispatch each complete line. Any trailing
        // partial line stays in m_readbuf for the next read.
        std::size_t start = 0;
        while (true) {
            const auto nl = m_readbuf.find('\n', start);
            if (nl == std::string::npos)
                break;
            std::string line = m_readbuf.substr(start, nl - start);
            start = nl + 1;
            if (!line.empty())
                dispatchLine(std::move(line));
        }
        if (start > 0)
            m_readbuf.erase(0, start);
    }

    void SocketClient::dispatchLine(std::string line) {
        try {
            const auto ev = parseEvent(line);
            std::visit([this](auto&& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, event::StateChanged>) {
                    if (m_callbacks.onState) m_callbacks.onState(x.value);
                } else if constexpr (std::is_same_v<T, event::Transcript>) {
                    if (m_callbacks.onTranscript) m_callbacks.onTranscript(x.text);
                } else if constexpr (std::is_same_v<T, event::Error>) {
                    if (m_callbacks.onError) m_callbacks.onError(x.message);
                } else if constexpr (std::is_same_v<T, event::StatusReply>) {
                    // Not consumed today. M2.6 will subscribe to
                    // status replies for the border-indicator config
                    // flag; adding it now avoids an unused-visitor
                    // warning in the interim.
                    (void)x;
                }
            }, ev);
        } catch (const ProtocolError& e) {
            log::warn("socket_client: bad event from daemon: {}", e.what());
        } catch (const std::exception& e) {
            log::warn("socket_client: exception dispatching event: {}", e.what());
        }
    }

    void SocketClient::handleDisconnect(const char* reason) {
        log::info("socket_client: disconnected ({})", reason);

        if (m_source) {
            wl_event_source_remove(m_source);
            m_source = nullptr;
        }
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }

        if (m_callbacks.onError)
            m_callbacks.onError(std::string{"daemon disconnected: "} + reason);

        scheduleReconnect();
    }

    int SocketClient::onRetryTimer(void* userdata) {
        auto* self = static_cast<SocketClient*>(userdata);

        // wl_event_loop timers are single-shot; the source is safe
        // to drop after firing. Zero it before attempting the
        // reconnect so a synchronous failure inside connect() can
        // schedule the next attempt without double-registering the
        // same timer slot.
        if (self->m_retryTimer) {
            wl_event_source_remove(self->m_retryTimer);
            self->m_retryTimer = nullptr;
        }

        // Grow the backoff for the next attempt (bounded). connect()
        // will reset it if this attempt succeeds.
        const uint32_t next = self->m_backoffMs * 2;
        self->m_backoffMs = (next > kBackoffMaxMs) ? kBackoffMaxMs : next;

        (void)self->connect();
        return 0;
    }

    void SocketClient::scheduleReconnect() {
        if (!g_pCompositor || !g_pCompositor->m_wlEventLoop) {
            // No event loop to schedule on. This branch is unusual —
            // PLUGIN_INIT runs after the compositor is up — but if
            // hit, subsequent dispatcher calls will keep firing
            // "daemon not connected" errors until PLUGIN_EXIT.
            return;
        }

        // If a retry is already queued, leave it — we don't want
        // multiple pending retries stacking up.
        if (m_retryTimer)
            return;

        m_retryTimer = wl_event_loop_add_timer(
            g_pCompositor->m_wlEventLoop,
            &SocketClient::onRetryTimer,
            this);
        if (!m_retryTimer) {
            log::warn("socket_client: wl_event_loop_add_timer failed; no reconnect");
            return;
        }

        wl_event_source_timer_update(m_retryTimer, static_cast<int>(m_backoffMs));
        log::info("socket_client: reconnect scheduled in {}ms", m_backoffMs);
    }

}
