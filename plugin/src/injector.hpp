#pragma once

// Deterministic-target text injection.
//
// The plugin's job at transcript time is to type text into the
// window that was focused when recording started, not whichever
// window currently has focus. The mechanism:
//
//   1. Look up the captured PHLWINDOWREF from M2.4 and its wlSurface.
//   2. Save g_pSeatManager->m_state.keyboardFocus so we can restore.
//   3. setKeyboardFocus(targetSurface) — routes future key events
//      to the target without touching Desktop::focusState (no visible
//      focus flash, no border/title-bar change).
//   4. Spawn `wtype <text>` — wtype connects as a Wayland client,
//      creates a zwp_virtual_keyboard_v1, and types. Its key events
//      go to the seat's current keyboard focus, which is the target.
//   5. Register the child's pidfd with Hyprland's wl_event_loop so
//      we get notified asynchronously when wtype exits.
//   6. On wtype exit, restore the saved keyboard focus.
//
// Everything else in the plugin is single-threaded on the compositor
// main thread; this class preserves that invariant. wtype runs as a
// separate process and is joined via pidfd + WL_EVENT_READABLE, not
// via a blocking waitpid.
//
// Concurrency: at most one injection is in flight (the daemon's
// state machine only produces one transcript per recording). A
// second startInject while one is pending is rejected with a warning
// — the caller should either queue or drop.

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

        // Fork+exec wtype pointed at targetWindow, register a pidfd
        // hook for its exit, and return. If targetWindow is null or
        // expired the call is a no-op returning false. If another
        // injection is pending, this one is rejected (false).
        bool startInject(PHLWINDOWREF targetWindow, const std::string& text);

        // True while a wtype process is running and the pidfd is
        // still registered with the event loop.
        bool isBusy() const noexcept { return m_pidfd >= 0; }

    private:
        static int onPidfdReady(int fd, uint32_t mask, void* userdata);

        void finishInject();

        pid_t                       m_pid    = -1;
        int                         m_pidfd  = -1;
        wl_event_source*            m_source = nullptr;
        WP<CWLSurfaceResource>      m_savedFocus;
    };

}
