#include "injector.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <spawn.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/View.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>

#include <wayland-server-core.h>

#include "log.hpp"

extern "C" char** environ;

namespace hyprdictate {

    namespace {

        // pidfd_open landed in Linux 5.3; glibc exposes a wrapper
        // from 2.36 onwards. Provide a syscall fallback so older
        // toolchains still build. Return -1 on failure with errno
        // set, matching the direct-syscall shape.
        int hyprdictate_pidfd_open(pid_t pid, unsigned int flags) {
#ifdef SYS_pidfd_open
            return static_cast<int>(::syscall(SYS_pidfd_open, pid, flags));
#else
            (void)pid;
            (void)flags;
            errno = ENOSYS;
            return -1;
#endif
        }

    }

    Injector::Injector() = default;

    Injector::~Injector() {
        // If a wtype is still in flight when the plugin unloads, we
        // detach it: remove the event source, close the pidfd, and
        // let init(1) reap the orphan on process exit. Waiting here
        // would risk holding up PLUGIN_EXIT for a runaway wtype.
        if (m_source) {
            wl_event_source_remove(m_source);
            m_source = nullptr;
        }
        if (m_pidfd >= 0) {
            ::close(m_pidfd);
            m_pidfd = -1;
        }
    }

    bool Injector::startInject(PHLWINDOWREF targetWindow, const std::string& text) {
        if (isBusy()) {
            log::warn("injector: prior wtype still running, rejecting new inject");
            return false;
        }

        const auto window = targetWindow.lock();
        if (!window) {
            log::warn("injector: target window expired before inject");
            return false;
        }

        const auto surface = window->wlSurface();
        if (!surface || !surface->resource()) {
            log::warn("injector: target window has no wl_surface");
            return false;
        }

        if (text.empty()) {
            log::debug("injector: empty transcript, skipping");
            return false;
        }

        if (!g_pSeatManager) {
            log::warn("injector: g_pSeatManager not available");
            return false;
        }

        // Snapshot the current keyboard focus so we can restore it
        // once wtype exits. Storing a weak pointer sidesteps the
        // "what if the previous focus target is destroyed mid-
        // injection" case: at restore time we lock() and simply skip
        // the restore if it's already gone.
        m_savedFocus = g_pSeatManager->m_state.keyboardFocus;

        // Redirect the seat's keyboard focus to the target so wtype's
        // virtual-keyboard events arrive at that surface. Note this
        // does NOT touch Desktop::focusState — the visible focused
        // window (border, title bar) stays where the user left it,
        // avoiding a distracting flash during injection.
        g_pSeatManager->setKeyboardFocus(surface->resource());

        // wtype takes the string to type as positional args; each
        // argv element is typed verbatim. Because we use execvp
        // (no shell), we don't need to escape anything.
        const char* argv[] = {
            "wtype",
            text.c_str(),
            nullptr,
        };

        pid_t pid = -1;
        const int spawn_rc = posix_spawnp(
            &pid, "wtype",
            /*file_actions=*/nullptr,
            /*attrp=*/nullptr,
            const_cast<char* const*>(argv),
            environ);
        if (spawn_rc != 0) {
            log::warn("injector: posix_spawnp(wtype) failed: {}",
                      std::strerror(spawn_rc));
            // Restore focus immediately; nothing else to clean up.
            if (auto prev = m_savedFocus.lock())
                g_pSeatManager->setKeyboardFocus(prev);
            m_savedFocus.reset();
            return false;
        }

        // Register a pidfd with Hyprland's own event loop so wtype's
        // exit fires as a compositor-main-thread callback. This is
        // the piece that keeps everything single-threaded: we don't
        // waitpid inline (deadlock, since wtype needs the wayland
        // server to service its requests) and we don't spin up a
        // reaper thread.
        const int pfd = hyprdictate_pidfd_open(pid, 0);
        if (pfd < 0) {
            log::warn("injector: pidfd_open failed: {}; injection continues but "
                      "focus restore will not fire",
                      std::strerror(errno));
            // wtype will still type; we just leak the focus
            // redirection. Reset saved-focus so a later inject
            // doesn't try to double-restore.
            m_savedFocus.reset();
            return true;
        }

        if (!g_pCompositor || !g_pCompositor->m_wlEventLoop) {
            log::warn("injector: compositor wl_event_loop unavailable");
            ::close(pfd);
            m_savedFocus.reset();
            return true;
        }

        m_pid    = pid;
        m_pidfd  = pfd;
        m_source = wl_event_loop_add_fd(
            g_pCompositor->m_wlEventLoop,
            pfd,
            WL_EVENT_READABLE,
            &Injector::onPidfdReady,
            this);
        if (!m_source) {
            log::warn("injector: wl_event_loop_add_fd for pidfd failed");
            ::close(pfd);
            m_pidfd  = -1;
            m_pid    = -1;
            m_savedFocus.reset();
            return true;
        }

        log::info("injector: wtype spawned pid={}, {} chars", pid, text.size());
        return true;
    }

    int Injector::onPidfdReady(int /*fd*/, uint32_t /*mask*/, void* userdata) {
        auto* self = static_cast<Injector*>(userdata);
        self->finishInject();
        return 0;
    }

    void Injector::finishInject() {
        // Reap the child. pidfd firing means the process has exited;
        // waitpid will return immediately with the status.
        int status = 0;
        if (m_pid > 0)
            ::waitpid(m_pid, &status, WNOHANG);

        if (WIFEXITED(status)) {
            const int code = WEXITSTATUS(status);
            if (code == 0)
                log::info("injector: wtype pid={} exited 0", m_pid);
            else
                log::warn("injector: wtype pid={} exited {}", m_pid, code);
        } else if (WIFSIGNALED(status)) {
            log::warn("injector: wtype pid={} killed by signal {}",
                      m_pid, WTERMSIG(status));
        }

        if (m_source) {
            wl_event_source_remove(m_source);
            m_source = nullptr;
        }
        if (m_pidfd >= 0) {
            ::close(m_pidfd);
            m_pidfd = -1;
        }
        m_pid = -1;

        // Restore the previous keyboard focus, if the surface is
        // still alive. Skipping when it's expired is deliberate:
        // handing focus to a destroyed surface is a compositor bug.
        if (g_pSeatManager) {
            if (auto prev = m_savedFocus.lock())
                g_pSeatManager->setKeyboardFocus(prev);
        }
        m_savedFocus.reset();
    }

}
