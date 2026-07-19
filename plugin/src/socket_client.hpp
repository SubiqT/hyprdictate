#pragma once

// Daemon socket client running on Hyprland's own wl_event_loop.
//
// The plugin dials $XDG_RUNTIME_DIR/hyprdictate.sock at PLUGIN_INIT
// time, sends {"cmd":"identify","role":"plugin"} so the daemon
// knows to suppress its wtype fallback (M2.8), and registers the
// socket fd with the compositor's own wl_event_loop via
// wl_event_loop_add_fd. Every event delivered by the daemon fires
// as a callback on Hyprland's main thread — no worker threads, no
// mutex discipline on plugin state.
//
// On disconnect (daemon restart, session graphical.target churn),
// the client schedules a reconnect via wl_event_loop_add_timer
// with exponential backoff (500ms → 1s → 2s → 5s → 10s → 30s cap).
// The backoff resets on any successful connect+identify. This is
// essential in practice because systemd tears the daemon down and
// back up around every login, and the plugin's PLUGIN_INIT runs
// during the exact window that also gets the daemon restarted.

#include <cstdint>
#include <functional>
#include <string>

#include "hyprdictate/protocol.hpp"
#include "hyprdictate/state.hpp"

struct wl_event_source;

namespace hyprdictate {

    class SocketClient {
    public:
        // Callbacks the plugin registers before calling connect().
        // Every callback runs on Hyprland's main thread.
        struct Callbacks {
            std::function<void(State)>             onState;
            std::function<void(const std::string&)> onTranscript;
            std::function<void(const std::string&)> onError;
        };

        // Ctor stores the callbacks. connect() actually opens the
        // socket, so a plugin that wants to defer wiring can do so
        // (though M2 constructs and connects at PLUGIN_INIT time).
        explicit SocketClient(Callbacks callbacks);
        ~SocketClient();

        SocketClient(const SocketClient&)            = delete;
        SocketClient& operator=(const SocketClient&) = delete;

        // Resolve the socket path, connect, register with the
        // compositor's wl_event_loop, and send the identify
        // handshake. On failure schedules a retry via the backoff
        // timer so callers see eventual connection even if the
        // daemon isn't up at PLUGIN_INIT time. Returns true only on
        // a synchronously-successful connect+identify.
        bool connect();

        // Send a command over the socket. Serialised inline (blocking
        // write) — commands are tiny JSON messages and there is no
        // benefit to queuing them. Returns false on write failure;
        // the client marks itself disconnected and the caller may
        // surface an error notification.
        bool send(const Command& cmd);

        // True once connect() has succeeded and the fd hasn't been
        // torn down by an EOF or error on the read path.
        bool isConnected() const noexcept { return m_fd >= 0; }

    private:
        // Static trampoline for wl_event_loop_add_fd. mask is a
        // bitmask of WL_EVENT_READABLE|WRITABLE|HANGUP|ERROR.
        static int onFdReady(int fd, uint32_t mask, void* userdata);

        // Static trampoline for wl_event_loop_add_timer. Fires the
        // scheduled reconnect attempt on the compositor main thread.
        static int onRetryTimer(void* userdata);

        void handleReadable();
        void handleDisconnect(const char* reason);
        void dispatchLine(std::string line);

        // Schedule the next reconnect attempt. Grows m_backoffMs
        // toward kBackoffMaxMs on each call and resets on successful
        // connect. Idempotent: safe to call from every code path
        // that transitions the client to disconnected.
        void scheduleReconnect();

        // Backoff bounds, in milliseconds. Starts short so the
        // typical login-race reconnect fires within a second, and
        // grows so a persistently-absent daemon doesn't hammer the
        // socket.
        static constexpr uint32_t kBackoffInitMs = 500;
        static constexpr uint32_t kBackoffMaxMs  = 30'000;

        Callbacks         m_callbacks;
        int               m_fd     = -1;
        wl_event_source*  m_source = nullptr;
        wl_event_source*  m_retryTimer = nullptr;
        uint32_t          m_backoffMs  = kBackoffInitMs;
        // Line-buffered read state. Bytes arrive in arbitrary
        // chunks; we accumulate here and split on '\n'.
        std::string       m_readbuf;
    };

}
