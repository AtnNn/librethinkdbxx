#include "connection.h"
#include "connection_p.h"
#include "json_p.h"
#include "exceptions.h"
#include "term.h"
#include "cursor_p.h"

namespace RethinkDB {

using QueryType = Protocol::Query::QueryType;
using ResponseType = Protocol::Response::ResponseType;

// TODO: make these non-global
bool asio_initialized = false;
asio::io_service io_service;
asio::io_service::work work(io_service);
std::thread io_service_thread;

// constants
const int debug_net = 0;
const uint32_t version_magic =
    static_cast<uint32_t>(Protocol::VersionDummy::Version::V0_4);
const uint32_t json_magic =
    static_cast<uint32_t>(Protocol::VersionDummy::Protocol::JSON);

Connection::Connection(Connection&&) noexcept = default;
std::unique_ptr<Connection> connect(std::string host, int port, std::string auth_key) {
   if (!asio_initialized) {
        fprintf(stderr, "initializing asio\n");
        io_service_thread = std::thread([](){ io_service.run(); });
        asio_initialized = true;
    }

    // TODO: connect is presently blocking, consider async_connect in the future
    tcp::socket socket(io_service);
    tcp::resolver resolver(io_service);

    try {
        asio::connect(socket, resolver.resolve({ host, std::to_string(port) }));

        // set socket options
        socket.set_option(tcp::no_delay(true));

        // sync write connection header
        {
            size_t size = auth_key.size();
            char buf[12 + size];
            memcpy(buf, &version_magic, 4);
            uint32_t n = size;
            memcpy(buf + 4, &n, 4);
            memcpy(buf + 8, auth_key.data(), size);
            memcpy(buf + 8 + size, &json_magic, 4);
            asio::write(socket, asio::buffer(buf, sizeof(buf)));
        }

        // sync receive reply
        {
            asio::streambuf response;
            asio::read_until(socket, response, '\0');
            if (strcmp(asio::buffer_cast<const char*>(response.data()), "SUCCESS")) {
                try {
                    socket.close();
                } catch (...) { }

                throw Error("Server rejected connection with message: %s",
                            asio::buffer_cast<const char*>(response.data()));
            }
        }
    } catch (std::exception& e) {
        fprintf(stderr, "connect exception: %s\n", e.what());
    }

    std::unique_ptr<Connection> conn(
        new Connection(new ConnectionPrivate(host, port, auth_key, std::move(socket))));
    conn->d->start_read_loop();
    return conn;
}

Connection::Connection(ConnectionPrivate *dd)
    : d(dd)
{ }

Connection::~Connection() {
    close();
}

void Connection::close() {
    CacheLock guard(*d);
    for (auto& it : d->guarded_cache) {
        stop_query(it.first);
    }

    int ret = ::close(d->guarded_sockfd);
    if (ret == -1) {
        throw Error::from_errno("close");
    }

    // TODO: this assumes a single connection, ref count multiple connections
    //       and call this when fully dereffed
    io_service.stop();
    io_service_thread.join();
    d->socket.close();
}

void ConnectionPrivate::run_query(Query query, bool no_reply) {
    std::string query_str = query.serialize();
    asio::async_write(socket, asio::buffer(query_str.data(), query_str.size()),
        [this, query, query_str](asio::error_code ec, std::size_t bytesWritten) {
            if (ec) {
                throw Error("write error: %s", ec.message().c_str());
            }
        });
}

Cursor Connection::start_query(Term *term, OptArgs&& opts) {
    bool no_reply = false;
    auto it = opts.find("noreply");
    if (it != opts.end()) {
        no_reply = *(it->second.datum.get_boolean());
    }

    uint64_t token = d->new_token();
    {
        CacheLock guard(*d);
        d->guarded_cache[token];
    }

    d->run_query(Query{QueryType::START, token, term->datum, std::move(opts)});
    if (no_reply) {
        return Cursor(new CursorPrivate(token, this, Nil()));
    }

    Cursor cursor(new CursorPrivate(token, this));
    Response response = d->wait_for_response(token, FOREVER);
    cursor.d->add_response(std::move(response));
    return cursor;
}

void Connection::stop_query(uint64_t token) {
    const auto& it = d->guarded_cache.find(token);
    if (it != d->guarded_cache.end() && !it->second.closed) {
        d->run_query(Query{QueryType::STOP, token}, true);
    }
}

void Connection::continue_query(uint64_t token) {
    d->run_query(Query{QueryType::CONTINUE, token}, true);
}

void ConnectionPrivate::start_read_loop() {
    asio::post(io_service, [this]() { read_loop(); });
}

void ConnectionPrivate::read_loop() {
    socket.async_read_some(asio::buffer(buffer),
        [this](std::error_code ec, std::size_t bytes_read) {
            if (!ec) {
                uint64_t token;
                ResponseParser::result_type result;
                std::tie(result, std::ignore, token) = parser.parse(
                    current_response, buffer.data(), buffer.data() + bytes_read);

                if (result == ResponseParser::good) {
                    if (debug_net > 0) {
                        fprintf(stderr, "[%" PRIu64 "] << %s\n", token, write_datum(current_response.result).c_str());
                    }

                    auto it = guarded_cache.find(token);
                    if (it == guarded_cache.end()) {
                        // drop the response
                    } else if (!it->second.closed) {
                        it->second.responses.emplace(std::move(current_response));
                        if (current_response.type != ResponseType::SUCCESS_PARTIAL) {
                            it->second.closed = true;
                        }
                    }

                    it->second.cond.notify_one();

                } else if (result == ResponseParser::bad) {
                    fprintf(stderr, "bad response parsing\n");
                }
            } else {
                throw Error("read error: %s", ec.message().c_str());
                // fprintf(stderr, "read_loop ec: %s\n", ec.message().c_str());
                // fprintf(stderr, "read: %s\n", buffer.data());
                // cond.notify_all();
            }

            read_loop();    // pump the loop again
        });
}

Response ConnectionPrivate::wait_for_response(uint64_t token_want, double wait) {
    CacheLock guard(*this);
    ConnectionPrivate::TokenCache& cache = guarded_cache[token_want];

    auto start = std::chrono::steady_clock::now();
    auto deadline = wait * 1000;
    while (true) {
        if (wait != FOREVER) {
            auto end = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(end - start).count() > deadline) {
                throw TimeoutException();
            }
        }

        if (!cache.responses.empty()) {
            Response response(std::move(cache.responses.front()));
            cache.responses.pop();
            if (cache.closed && cache.responses.empty()) {
                guarded_cache.erase(token_want);
            }

            return response;
        }

        if (cache.closed) {
            throw Error("Trying to read from a closed token");
        }

        cache.cond.wait_for(guard.inner_lock, std::chrono::milliseconds(100));
    }
}

// RESPONSE
Error Response::as_error() {
    std::string repr;
    if (result.size() == 1) {
        std::string* string = result[0].get_string();
        if (string) {
            repr = *string;
        } else {
            repr = write_datum(result[0]);
        }
    } else {
        repr = write_datum(Datum(result));
    }
    std::string err;
    using RT = Protocol::Response::ResponseType;
    using ET = Protocol::Response::ErrorType;
    switch (type) {
    case RT::SUCCESS_SEQUENCE: err = "unexpected response: SUCCESS_SEQUENCE"; break;
    case RT::SUCCESS_PARTIAL:  err = "unexpected response: SUCCESS_PARTIAL"; break;
    case RT::SUCCESS_ATOM: err = "unexpected response: SUCCESS_ATOM"; break;
    case RT::WAIT_COMPLETE: err = "unexpected response: WAIT_COMPLETE"; break;
    case RT::SERVER_INFO: err = "unexpected response: SERVER_INFO"; break;
    case RT::CLIENT_ERROR: err = "ReqlDriverError"; break;
    case RT::COMPILE_ERROR: err = "ReqlCompileError"; break;
    case RT::RUNTIME_ERROR:
        switch (error_type) {
        case ET::INTERNAL: err = "ReqlInternalError"; break;
        case ET::RESOURCE_LIMIT: err = "ReqlResourceLimitError"; break;
        case ET::QUERY_LOGIC: err = "ReqlQueryLogicError"; break;
        case ET::NON_EXISTENCE: err = "ReqlNonExistenceError"; break;
        case ET::OP_FAILED: err = "ReqlOpFailedError"; break;
        case ET::OP_INDETERMINATE: err = "ReqlOpIndeterminateError"; break;
        case ET::USER: err = "ReqlUserError"; break;
        case ET::PERMISSION_ERROR: err = "ReqlPermissionError"; break;
        default: err = "ReqlRuntimeError"; break;
        }
    }
    throw Error("%s: %s", err.c_str(), repr.c_str());
}

Protocol::Response::ResponseType response_type(double t) {
    int n = static_cast<int>(t);
    using RT = Protocol::Response::ResponseType;
    switch (n) {
    case static_cast<int>(RT::SUCCESS_ATOM):
        return RT::SUCCESS_ATOM;
    case static_cast<int>(RT::SUCCESS_SEQUENCE):
        return RT::SUCCESS_SEQUENCE;
    case static_cast<int>(RT::SUCCESS_PARTIAL):
        return RT::SUCCESS_PARTIAL;
    case static_cast<int>(RT::WAIT_COMPLETE):
        return RT::WAIT_COMPLETE;
    case static_cast<int>(RT::CLIENT_ERROR):
        return RT::CLIENT_ERROR;
    case static_cast<int>(RT::COMPILE_ERROR):
        return RT::COMPILE_ERROR;
    case static_cast<int>(RT::RUNTIME_ERROR):
        return RT::RUNTIME_ERROR;
    default:
        throw Error("Unknown response type");
    }
}

Protocol::Response::ErrorType runtime_error_type(double t) {
    int n = static_cast<int>(t);
    using ET = Protocol::Response::ErrorType;
    switch (n) {
    case static_cast<int>(ET::INTERNAL):
        return ET::INTERNAL;
    case static_cast<int>(ET::RESOURCE_LIMIT):
        return ET::RESOURCE_LIMIT;
    case static_cast<int>(ET::QUERY_LOGIC):
        return ET::QUERY_LOGIC;
    case static_cast<int>(ET::NON_EXISTENCE):
        return ET::NON_EXISTENCE;
    case static_cast<int>(ET::OP_FAILED):
        return ET::OP_FAILED;
    case static_cast<int>(ET::OP_INDETERMINATE):
        return ET::OP_INDETERMINATE;
    case static_cast<int>(ET::USER):
        return ET::USER;
    default:
        throw Error("Unknown error type");
    }
}

}
