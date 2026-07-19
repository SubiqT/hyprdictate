#include "dispatcher.hpp"

#include <optional>
#include <string>
#include <string_view>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include "globals.hpp"
#include "hyprdictate/protocol.hpp"
#include "log.hpp"

namespace hyprdictate {

    namespace {

        SDispatchResult okReply() {
            return SDispatchResult{
                .passEvent = false,
                .success   = true,
                .error     = "",
            };
        }

        SDispatchResult errReply(std::string msg) {
            log::warn("dispatcher: {}", msg);
            return SDispatchResult{
                .passEvent = false,
                .success   = false,
                .error     = std::move(msg),
            };
        }

        SDispatchResult sendToDaemon(const Command& cmd, std::string_view label) {
            if (!g_plugin.socket) {
                return errReply(std::string{label}
                                + ": plugin socket client not initialised");
            }
            if (!g_plugin.socket->isConnected()) {
                return errReply(std::string{label}
                                + ": daemon not connected");
            }
            if (!g_plugin.socket->send(cmd)) {
                return errReply(std::string{label} + ": socket write failed");
            }
            return okReply();
        }

        // Snapshot the currently-focused window into g_plugin.
        // targetWindow and build a WindowContext for the outgoing
        // `start` payload. Returns nullopt if there's no focused
        // window (e.g. an empty workspace); the daemon then falls
        // back to global vocabulary alone.
        std::optional<WindowContext> captureFocusedWindow() {
            const auto focus = Desktop::focusState();
            if (!focus)
                return std::nullopt;

            const auto window = focus->window();
            if (!window) {
                g_plugin.targetWindow.reset();
                return std::nullopt;
            }

            g_plugin.targetWindow = window;

            return WindowContext{
                .cls   = window->m_class,
                .title = window->m_title,
            };
        }

        // Toggle is the CLI-shaped edge action; the plugin maps it
        // onto the daemon's level-triggered start/stop pair based on
        // the locally-mirrored state so start payloads always carry
        // a window context. If we sent bare `toggle` the daemon
        // couldn't attach class/title to the vocabulary.
        SDispatchResult dispatchToggle(std::string /*args*/) {
            if (g_plugin.daemonState == State::Recording) {
                return sendToDaemon(command::Stop{}, "toggle→stop");
            }
            if (g_plugin.daemonState != State::Idle) {
                // Transcribing / Error / Cancelled: forward as toggle
                // and let the daemon's handleToggle decide (it drops
                // it as a no-op). Keeps the dispatcher symmetric.
                return sendToDaemon(command::Toggle{}, "toggle");
            }

            const auto window = captureFocusedWindow();
            return sendToDaemon(command::Start{ .window = window }, "toggle→start");
        }

        SDispatchResult dispatchPttDown(std::string /*args*/) {
            const auto window = captureFocusedWindow();
            return sendToDaemon(command::PttDown{ .window = window }, "ptt_down");
        }

        SDispatchResult dispatchPttUp(std::string /*args*/) {
            return sendToDaemon(command::PttUp{}, "ptt_up");
        }

        SDispatchResult dispatchCancel(std::string /*args*/) {
            g_plugin.targetWindow.reset();
            return sendToDaemon(command::Cancel{}, "cancel");
        }

        // Lua thunks. Hyprland 0.55+ exposes plugin operations under
        // hl.plugin.<namespace>.<name>. `hyprctl dispatch` also
        // routes through Lua, so an addDispatcherV2 handler alone
        // isn't reachable via `hyprctl dispatch
        // 'hl.plugin.hyprdictate.toggle()'` unless we also register
        // Lua thunks.
        //
        // Return contract (hyprwsmode's precedent): the Lua thunk
        // performs its side effect and pushes hl.dsp.no_op() as a
        // valid dispatcher table so hl.bind callbacks have something
        // to invoke.
        void pushNoOp(lua_State* L) {
            lua_getglobal(L, "hl");
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return;
            }

            lua_getfield(L, -1, "dsp");
            lua_remove(L, -2);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return;
            }

            lua_getfield(L, -1, "no_op");
            lua_remove(L, -2);
            if (!lua_isfunction(L, -1)) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return;
            }

            if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
                const char* errMsg = lua_tostring(L, -1);
                log::warn("hl.dsp.no_op() failed: {}",
                          errMsg ? errMsg : "(no message)");
                lua_pop(L, 1);
                lua_pushnil(L);
            }
        }

        int lua_toggle(lua_State* L) {
            dispatchToggle({});
            pushNoOp(L);
            return 1;
        }

        int lua_ptt_down(lua_State* L) {
            dispatchPttDown({});
            pushNoOp(L);
            return 1;
        }

        int lua_ptt_up(lua_State* L) {
            dispatchPttUp({});
            pushNoOp(L);
            return 1;
        }

        int lua_cancel(lua_State* L) {
            dispatchCancel({});
            pushNoOp(L);
            return 1;
        }

    }  // namespace

    void registerDispatchers() {
        HyprlandAPI::addDispatcherV2(PHANDLE, "hyprdictate:toggle",   &dispatchToggle);
        HyprlandAPI::addDispatcherV2(PHANDLE, "hyprdictate:ptt_down", &dispatchPttDown);
        HyprlandAPI::addDispatcherV2(PHANDLE, "hyprdictate:ptt_up",   &dispatchPttUp);
        HyprlandAPI::addDispatcherV2(PHANDLE, "hyprdictate:cancel",   &dispatchCancel);

        struct SLuaBinding {
            const char*    name;
            PLUGIN_LUA_FN  fn;
        };
        static const SLuaBinding bindings[] = {
            {"toggle",   &lua_toggle},
            {"ptt_down", &lua_ptt_down},
            {"ptt_up",   &lua_ptt_up},
            {"cancel",   &lua_cancel},
        };

        for (const auto& b : bindings) {
            if (!HyprlandAPI::addLuaFunction(PHANDLE, "hyprdictate", b.name, b.fn))
                log::warn("addLuaFunction failed for hl.plugin.hyprdictate.{}", b.name);
        }
    }

}
