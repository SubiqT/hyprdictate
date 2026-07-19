#include "dispatcher.hpp"

#include <string>
#include <string_view>

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

        // Send a command to the daemon and translate the result into
        // a dispatcher-shaped return. Kept in one helper so every
        // dispatcher path exits through the same success/error mould
        // and dispatches don't drift in surface behaviour.
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

        SDispatchResult dispatchToggle(std::string /*args*/) {
            // Window context is populated in M2.4 (focused surface
            // capture on the recording-start transition). For M2.3
            // we hand the daemon an empty toggle and it composes
            // vocabulary from globals alone.
            return sendToDaemon(command::Toggle{}, "toggle");
        }

        SDispatchResult dispatchPttDown(std::string /*args*/) {
            return sendToDaemon(command::PttDown{}, "ptt_down");
        }

        SDispatchResult dispatchPttUp(std::string /*args*/) {
            return sendToDaemon(command::PttUp{}, "ptt_up");
        }

        SDispatchResult dispatchCancel(std::string /*args*/) {
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
        // to invoke. Without this the action fires but the bind
        // silently no-ops on subsequent presses.

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
        // Classic addDispatcherV2 registrations. Bind with:
        //   bind = SUPER, H, hyprdictate, toggle
        HyprlandAPI::addDispatcherV2(PHANDLE, "hyprdictate:toggle",   &dispatchToggle);
        HyprlandAPI::addDispatcherV2(PHANDLE, "hyprdictate:ptt_down", &dispatchPttDown);
        HyprlandAPI::addDispatcherV2(PHANDLE, "hyprdictate:ptt_up",   &dispatchPttUp);
        HyprlandAPI::addDispatcherV2(PHANDLE, "hyprdictate:cancel",   &dispatchCancel);

        // Lua namespace: hl.plugin.hyprdictate.<fn>. Registered per
        // subcommand rather than as a single dispatcher-string so
        // typos in Lua fail loudly at call time instead of silently
        // reaching an unknown-command branch.
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
