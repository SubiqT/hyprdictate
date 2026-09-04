#include "injector.hpp"

#include <cerrno>
#include <csignal>
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
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>

#include <wayland-server-core.h>

#include "injection_gate.hpp"
#include "log.hpp"

extern "C" char** environ;

namespace hyprdictate {

    namespace {

        int hyprdictatePidfdOpen(pid_t pid, unsigned int flags) {
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
        clearPending();
        terminateChild();
    }

    bool Injector::startInject(PHLWINDOWREF targetWindow, const std::string& text) {
        if (isBusy()) {
            log::warn("injector: previous injection still pending, rejecting new text");
            return false;
        }
        if (!targetWindow.lock()) {
            log::warn("injector: target window expired before queueing");
            return false;
        }
        if (text.empty()) {
            log::debug("injector: empty transcript, skipping");
            return false;
        }
        if (!g_pCompositor || !g_pCompositor->m_wlEventLoop || !g_pInputManager) {
            log::warn("injector: compositor input/event loop unavailable");
            return false;
        }

        m_pendingWindow = targetWindow;
        m_pendingText   = text;
        m_deadline      = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(kInjectionTimeoutMs);
        m_releaseTimer  = wl_event_loop_add_timer(
            g_pCompositor->m_wlEventLoop,
            &Injector::onReleaseTimer,
            this);
        if (!m_releaseTimer) {
            log::warn("injector: failed to create input-release timer");
            clearPending();
            return false;
        }

        // Always defer at least one event-loop turn. The stop dispatcher is
        // normally triggered by SUPER+H; redirecting focus synchronously would
        // send that physical key's release to a different surface.
        wl_event_source_timer_update(m_releaseTimer, static_cast<int>(kInjectionPollMs));
        log::info("injector: queued {} chars until physical input is released",
                  text.size());
        return true;
    }

    int Injector::onReleaseTimer(void* userdata) {
        return static_cast<Injector*>(userdata)->pollInputRelease();
    }

    int Injector::pollInputRelease() {
        if (!m_releaseTimer)
            return 0;

        const bool anyInput = g_pInputManager &&
                              (!g_pInputManager->getKeysFromAllKBs().empty() ||
                               g_pInputManager->hasHeldButtons());
        const std::uint32_t modifiers = g_pInputManager
            ? g_pInputManager->getModsFromAllKBs()
            : 1;
        const bool timedOut = std::chrono::steady_clock::now() >= m_deadline;

        switch (injectionGateDecision(anyInput, modifiers, timedOut)) {
            case InjectionGateDecision::Wait:
                wl_event_source_timer_update(
                    m_releaseTimer, static_cast<int>(kInjectionPollMs));
                return 0;

            case InjectionGateDecision::Drop:
                log::warn("injector: physical input remained active past {}ms; "
                          "dropping transcript for safety", kInjectionTimeoutMs);
                clearPending();
                return 0;

            case InjectionGateDecision::Inject: {
                auto window = m_pendingWindow;
                auto text   = std::move(m_pendingText);
                clearPending();
                (void)launchInject(window, text);
                return 0;
            }
        }
        return 0;
    }

    bool Injector::launchInject(PHLWINDOWREF targetWindow, const std::string& text) {
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
        if (!g_pSeatManager) {
            log::warn("injector: seat manager unavailable");
            return false;
        }

        // Re-check immediately before redirecting focus. A key may have been
        // pressed between the timer decision and this call.
        if (std::chrono::steady_clock::now() >= m_deadline ||
            !g_pInputManager || !g_pInputManager->getKeysFromAllKBs().empty() ||
            g_pInputManager->hasHeldButtons() ||
            g_pInputManager->getModsFromAllKBs() != 0) {
            log::warn("injector: physical input or timeout changed during launch; "
                      "dropping transcript");
            return false;
        }

        m_savedFocus = g_pSeatManager->m_state.keyboardFocus;
        g_pSeatManager->setKeyboardFocus(surface->resource());

        const char* argv[] = {
            "wtype",
            text.c_str(),
            nullptr,
        };

        pid_t pid = -1;
        const int spawnResult = posix_spawnp(
            &pid,
            "wtype",
            nullptr,
            nullptr,
            const_cast<char* const*>(argv),
            environ);
        if (spawnResult != 0) {
            log::warn("injector: posix_spawnp(wtype) failed: {}",
                      std::strerror(spawnResult));
            restoreFocus();
            return false;
        }

        m_pid = pid;
        const int pidfd = hyprdictatePidfdOpen(pid, 0);
        if (pidfd < 0) {
            log::warn("injector: pidfd_open failed: {}; terminating wtype",
                      std::strerror(errno));
            terminateChild();
            return false;
        }

        m_pidfd = pidfd;
        m_source = wl_event_loop_add_fd(
            g_pCompositor->m_wlEventLoop,
            pidfd,
            WL_EVENT_READABLE,
            &Injector::onPidfdReady,
            this);
        if (!m_source) {
            log::warn("injector: failed to monitor wtype; terminating it");
            terminateChild();
            return false;
        }

        log::info("injector: wtype spawned pid={}, {} chars", pid, text.size());
        return true;
    }

    int Injector::onPidfdReady(int /*fd*/, uint32_t /*mask*/, void* userdata) {
        static_cast<Injector*>(userdata)->finishInject();
        return 0;
    }

    void Injector::finishInject() {
        int status = 0;
        if (m_pid > 0)
            ::waitpid(m_pid, &status, WNOHANG);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            log::info("injector: wtype pid={} exited 0", m_pid);
        else if (WIFEXITED(status))
            log::warn("injector: wtype pid={} exited {}", m_pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            log::warn("injector: wtype pid={} killed by signal {}",
                      m_pid, WTERMSIG(status));

        if (m_source) {
            wl_event_source_remove(m_source);
            m_source = nullptr;
        }
        if (m_pidfd >= 0) {
            ::close(m_pidfd);
            m_pidfd = -1;
        }
        m_pid = -1;
        restoreFocus();
    }

    void Injector::clearPending() {
        if (m_releaseTimer) {
            wl_event_source_remove(m_releaseTimer);
            m_releaseTimer = nullptr;
        }
        m_pendingWindow.reset();
        m_pendingText.clear();
    }

    void Injector::restoreFocus() {
        if (g_pSeatManager) {
            if (auto previous = m_savedFocus.lock())
                g_pSeatManager->setKeyboardFocus(previous);
        }
        m_savedFocus.reset();
    }

    void Injector::terminateChild() {
        if (m_source) {
            wl_event_source_remove(m_source);
            m_source = nullptr;
        }
        if (m_pidfd >= 0) {
            ::close(m_pidfd);
            m_pidfd = -1;
        }
        if (m_pid > 0) {
            // Failure paths run only when pidfd monitoring could not be
            // established or during plugin teardown. SIGKILL guarantees the
            // child cannot emit later virtual-keyboard events; blocking waitpid
            // then guarantees it is reaped before focus is restored.
            ::kill(m_pid, SIGKILL);
            int status = 0;
            while (::waitpid(m_pid, &status, 0) < 0 && errno == EINTR) {}
            m_pid = -1;
        }
        restoreFocus();
    }

}
