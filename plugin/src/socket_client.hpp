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
// If the connect fails (daemon down, socket missing), the plugin
// still loads: dispatchers will emit an error notification when the
// user triggers one, but the compositor keeps running. Reconnect
// is not attempted; a compositor restart (`hyprctl reload` for the
// plugin) after starting the daemon brings the client back up.

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
        // handshake. Returns true on success; on failure the client
        // logs the reason and leaves itself in an unconnected state
        // so dispatchers can report it.
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

        void handleReadable();
        void handleDisconnect(const char* reason);
        void dispatchLine(std::string line);

        Callbacks         m_callbacks;
        int               m_fd     = -1;
        wl_event_source*  m_source = nullptr;
        // Line-buffered read state. Bytes arrive in arbitrary
        // chunks; we accumulate here and split on '\n'.
        std::string       m_readbuf;
    };

}
