#pragma once

// Text injection via the `wtype` command-line tool.
//
// The daemon spawns wtype as a subprocess and pipes the transcript
// through stdin (wtype's `-` argument). Piping instead of passing on
// argv sidesteps shell metacharacter issues in the transcript and
// keeps the invocation independent of user locale.
//
// This is the M1 fallback path. M2 replaces it with the plugin-side
// wlr_virtual_keyboard_v1 injector, which types into the recording's
// start-time surface rather than whichever window has focus at inject
// time. wtype only sees "the window currently focused when the
// subprocess types keys", which is why the design doc flags it as a
// fallback.

#include <optional>
#include <string>

#include "hyprdictate/protocol.hpp"

namespace hyprdictate {

    class WtypeInjector {
    public:
        WtypeInjector();
        ~WtypeInjector() = default;

        WtypeInjector(const WtypeInjector&)            = delete;
        WtypeInjector& operator=(const WtypeInjector&) = delete;

        // Inject `text` into the focused window. window is currently
        // unused (wtype has no window-targeting hook); it's threaded
        // through for parity with the M2 injector's signature. Runs
        // synchronously; the caller is expected to be on a worker
        // thread so the wtype subprocess doesn't block the IPC loop.
        //
        // Returns true on wtype exit status 0, false on any failure
        // (fork/exec/wait/non-zero exit). Failures are logged inside
        // the injector; callers just check the bool.
        bool inject(const std::string&                  text,
                    const std::optional<WindowContext>& window);
    };

}
