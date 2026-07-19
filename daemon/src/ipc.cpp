#include "ipc.hpp"

#include <system_error>
#include <utility>

#include <spdlog/spdlog.h>

#include "session.hpp"

namespace hyprdictate {

    namespace fs = std::filesystem;
    using unix_socket = asio::local::stream_protocol;

    // A connection reads one JSON message per newline, dispatches it
    // to the Session, unicasts any returned reply back, and remains
    // subscribed to broadcast events for the connection's lifetime.
    struct IpcServer::Connection : std::enable_shared_from_this<Connection> {
        Connection(IpcServer& server, unix_socket::socket socket)
            : m_server(server)
            , m_socket(std::move(socket))
        {}

        void start() {
            readNextLine();
        }

        void close() {
            std::error_code ec;
            m_socket.shutdown(unix_socket::socket::shutdown_both, ec);
            m_socket.close(ec);
        }

        // Serialize an event to line-delimited JSON and queue it for
        // writing. All queueing happens on the io_context thread, so
        // the outbound queue is single-writer / single-reader.
        void send(const Event& e) {
            const auto payload = serialize(e).dump() + "\n";
            const bool writing = !m_outbox.empty();
            m_outbox.push_back(std::move(payload));
            if (!writing)
                doWrite();
        }

        // Called when the connection ends (EOF, error, or shutdown).
        // If this connection had identified as a plugin, decrement
        // the server's plugin count so the daemon's wtype fallback
        // wakes back up.
        void onClosed() {
            if (m_isPlugin) {
                const int now = m_server.m_pluginCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
                spdlog::info("ipc: plugin disconnected ({} plugin connection(s) remaining, "
                             "wtype {})",
                             now, now == 0 ? "re-enabled" : "still suppressed");
                m_isPlugin = false;
            }
        }

    private:
        void readNextLine() {
            auto self = shared_from_this();
            asio::async_read_until(m_socket, m_readbuf, '\n',
                [self](const std::error_code& ec, std::size_t bytes) {
                    if (ec) {
                        // EOF or peer close: drop the connection.
                        self->onClosed();
                        self->m_server.removeConnection(self);
                        return;
                    }
                    self->onLine(bytes);
                });
        }

        void onLine(std::size_t bytes) {
            std::string line;
            line.resize(bytes);
            auto buf = m_readbuf.data();
            std::copy_n(asio::buffers_begin(buf), bytes, line.begin());
            m_readbuf.consume(bytes);

            // Strip trailing '\n' (and optional '\r' for tolerant
            // clients) before parsing so the JSON parser sees clean
            // input.
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();

            if (!line.empty()) {
                try {
                    auto cmd = parseCommand(line);

                    // Intercept Identify: update this connection's
                    // role and the server's plugin-count. Session
                    // still sees the command below for logging.
                    if (const auto* id = std::get_if<command::Identify>(&cmd)) {
                        if (id->role == "plugin" && !m_isPlugin) {
                            m_isPlugin = true;
                            const int now = m_server.m_pluginCount.fetch_add(1,
                                std::memory_order_acq_rel) + 1;
                            spdlog::info("ipc: plugin identified ({} plugin "
                                         "connection(s), wtype suppressed)",
                                         now);
                        }
                    }

                    if (auto reply = m_server.m_session.handle(cmd); reply)
                        send(*reply);
                } catch (const ProtocolError& e) {
                    spdlog::warn("ipc: protocol error: {}", e.what());
                    send(event::Error{ .message = e.what() });
                } catch (const std::exception& e) {
                    spdlog::error("ipc: unhandled exception on line: {}", e.what());
                    send(event::Error{ .message = std::string{"internal: "} + e.what() });
                }
            }

            readNextLine();
        }

        void doWrite() {
            if (m_outbox.empty()) return;

            auto self = shared_from_this();
            asio::async_write(m_socket, asio::buffer(m_outbox.front()),
                [self](const std::error_code& ec, std::size_t /*bytes*/) {
                    if (ec) {
                        self->onClosed();
                        self->m_server.removeConnection(self);
                        return;
                    }
                    self->m_outbox.pop_front();
                    self->doWrite();
                });
        }

        IpcServer&              m_server;
        unix_socket::socket     m_socket;
        asio::streambuf         m_readbuf;
        std::list<std::string>  m_outbox;

        // Set true when this connection has identified as
        // role="plugin". Read only from the io thread (Connection
        // callbacks all run there), so no atomicity needed.
        bool                    m_isPlugin = false;
    };

    IpcServer::IpcServer(asio::io_context&     io,
                         fs::path              socket_path,
                         Session&              session)
        : m_io(io)
        , m_socketPath(std::move(socket_path))
        , m_session(session)
        , m_acceptor(io)
    {}

    IpcServer::~IpcServer() {
        stop();
    }

    void IpcServer::start() {
        // Remove a stale socket file from a previous run. bind() would
        // otherwise fail with EADDRINUSE. This is racy against a
        // concurrent daemon binding the same path; running two
        // daemons is not a supported deployment.
        std::error_code ec;
        fs::remove(m_socketPath, ec);

        m_acceptor.open(unix_socket());
        m_acceptor.bind(unix_socket::endpoint(m_socketPath.string()));
        m_acceptor.listen();

        // Restrict the socket to the running user: 0600 keeps it out
        // of reach of other local accounts. XDG_RUNTIME_DIR already
        // enforces per-user isolation, but tightening here removes
        // the world-readable default from odd session-manager
        // configurations.
        fs::permissions(m_socketPath,
                        fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, ec);

        spdlog::info("ipc: listening on {}", m_socketPath.string());
        doAccept();
    }

    void IpcServer::stop() {
        std::error_code ec;
        m_acceptor.close(ec);

        std::list<std::shared_ptr<Connection>> conns;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            conns.swap(m_connections);
        }
        for (auto& c : conns) {
            c->onClosed();
            c->close();
        }

        fs::remove(m_socketPath, ec);
    }

    void IpcServer::broadcast(const Event& e) {
        // Hop onto the io_context thread before touching m_connections
        // or any socket. Session's transcribe worker calls this from
        // its own thread; asio's post() is the cheapest hand-off.
        asio::post(m_io, [this, e]() {
            std::list<std::shared_ptr<Connection>> snapshot;
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                snapshot = m_connections;
            }
            for (auto& c : snapshot)
                c->send(e);
        });
    }

    void IpcServer::doAccept() {
        m_acceptor.async_accept(
            [this](const std::error_code& ec, unix_socket::socket socket) {
                if (ec) {
                    // async_operation_aborted is the normal shutdown
                    // path; anything else is worth surfacing.
                    if (ec != asio::error::operation_aborted)
                        spdlog::warn("ipc: accept failed: {}", ec.message());
                    return;
                }
                auto conn = std::make_shared<Connection>(*this, std::move(socket));
                addConnection(conn);
                conn->start();
                doAccept();
            });
    }

    void IpcServer::addConnection(std::shared_ptr<Connection> conn) {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_connections.push_back(std::move(conn));
    }

    void IpcServer::removeConnection(const std::shared_ptr<Connection>& conn) {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_connections.remove_if([&](const std::shared_ptr<Connection>& c) {
            return c.get() == conn.get();
        });
    }

}
