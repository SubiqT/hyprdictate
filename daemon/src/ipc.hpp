#pragma once

// Unix-domain socket server for the daemon.
//
// Every command / event travels as one JSON object per newline-
// terminated line. Multiple clients (M1's CLI; M2's plugin; M3's
// widget) may be connected at once: commands are dispatched to the
// Session as they arrive, and events fan out to every open
// connection. Broadcasts are safe from any thread; the server posts
// each write onto its io_context so socket I/O stays serialised.

#include <filesystem>
#include <list>
#include <memory>
#include <mutex>

#include <asio.hpp>

#include "hyprdictate/protocol.hpp"

namespace hyprdictate {

    class Session;

    class IpcServer {
    public:
        IpcServer(asio::io_context&      io,
                  std::filesystem::path  socket_path,
                  Session&               session);
        ~IpcServer();

        IpcServer(const IpcServer&)            = delete;
        IpcServer& operator=(const IpcServer&) = delete;

        // Begin listening. Removes any stale socket at the configured
        // path (a common leftover after an ungraceful shutdown) so
        // hyprdictated can be restarted without manual cleanup.
        void start();

        // Close the acceptor, close every open connection, and remove
        // the socket file. Called from main() on shutdown so systemd's
        // ExecStopPost doesn't have to.
        void stop();

        // Fan out an event to every connected client. Safe to call
        // from any thread (posts onto m_io).
        void broadcast(const Event& e);

    private:
        struct Connection;

        void doAccept();
        void addConnection(std::shared_ptr<Connection> conn);
        void removeConnection(const std::shared_ptr<Connection>& conn);

        asio::io_context&                              m_io;
        std::filesystem::path                          m_socketPath;
        Session&                                       m_session;
        asio::local::stream_protocol::acceptor         m_acceptor;

        // Live connections. Guarded by m_mutex; the io thread manages
        // add/remove, and broadcast() posts onto io_context before it
        // reads the list so the reader always runs on the io thread.
        std::mutex                                     m_mutex;
        std::list<std::shared_ptr<Connection>>         m_connections;
    };

}
