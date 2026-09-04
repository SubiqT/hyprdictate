#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <sys/types.h>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>

class CWLSurfaceResource;
struct wl_event_source;

namespace hyprdictate {

    class Injector {
    public:
        Injector();
        ~Injector();

        Injector(const Injector&)            = delete;
        Injector& operator=(const Injector&) = delete;

        // Queue finalized text for deterministic-target injection. Injection
        // starts only after every physical key, pointer button, and depressed
        // modifier has been released; if that never happens within two
        // seconds, text is dropped rather than interpreted as compositor
        // shortcuts.
        bool startInject(PHLWINDOWREF targetWindow, const std::string& text);

        bool isBusy() const noexcept {
            return m_releaseTimer != nullptr || m_pidfd >= 0;
        }

    private:
        static int onReleaseTimer(void* userdata);
        static int onPidfdReady(int fd, uint32_t mask, void* userdata);

        int  pollInputRelease();
        bool launchInject(PHLWINDOWREF targetWindow, const std::string& text);
        void finishInject();
        void clearPending();
        void restoreFocus();
        void terminateChild();

        pid_t            m_pid          = -1;
        int              m_pidfd        = -1;
        wl_event_source* m_source       = nullptr;
        wl_event_source* m_releaseTimer = nullptr;

        PHLWINDOWREF m_pendingWindow;
        std::string  m_pendingText;
        std::chrono::steady_clock::time_point m_deadline{};
        WP<CWLSurfaceResource> m_savedFocus;
    };

}
