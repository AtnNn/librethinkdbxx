#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <memory>
#include <condition_variable>

#include "protocol_defs.h"
#include "datum.h"
#include "error.h"

#define FOREVER (-1)
#define SECOND 1
#define MICROSECOND 0.0000001

namespace RethinkDB {

class Token;
class Connection;

// Used internally to convert a raw response type into an enum
Protocol::Response::ResponseType response_type(double t);
Protocol::Response::ErrorType runtime_error_type(double t);

// Contains a response from the server. Use the Cursor class to interact with these responses
class Response {
public:
    Response(Datum&& datum) :
        type(response_type(std::move(datum).extract_field("t").extract_number())),
        error_type(datum.get_field("e") ?
                   runtime_error_type(std::move(datum).extract_field("e").extract_number()) :
                   Protocol::Response::ErrorType(0)),
        result(std::move(datum).extract_field("r").extract_array()) { }
    Error as_error();
    Protocol::Response::ResponseType type;
    Protocol::Response::ErrorType error_type;
    Array result;
};

// A connection to a RethinkDB server
// It contains:
//  * A socket
//  * Read and write locks
//  * A cache of responses that have not been read by the corresponding Cursor
class Connection {
public:
    Connection(const std::string& host, int port, const std::string& auth_key);

    Connection(const Connection&) = delete;
    Connection(Connection&&) = default;

    // Used internally by Term::run
    Token start_query(const std::string&);

    void close();

private:
    Response wait_for_response(uint64_t, double);
    void close_token(uint64_t);
    void ask_for_more(uint64_t);

    friend class SocketReadStream;
    friend class Token;

    class ReadLock;
    class WriteLock;
    class CacheLock;

    std::mutex read_lock;
    std::mutex write_lock;
    std::mutex cache_lock;

    struct TokenCache {
        bool closed = false;
        std::condition_variable cond;
        std::queue<Response> responses;
    };

    std::map<uint64_t, TokenCache> guarded_cache;
    uint64_t guarded_next_token;
    int guarded_sockfd;
    bool guarded_loop_active;
};

// $doc(connect)
std::unique_ptr<Connection> connect(std::string host = "localhost", int port = 28015, std::string auth_key = "");

// Each server response is associated with a token, which allows multiplexing streams
// A token can be used to create a cursor
class Token {
public:
    Token(uint64_t token = 0, Connection *conn = nullptr);
    Token(const Token&) = delete;
    Token(Token&& other);
    Token& operator=(Token&& other);
    ~Token();

    void ask_for_more() const;
    Response wait_for_response(double wait) const;
    void close() const;

private:
    friend class Connection;

    uint64_t token;
    Connection* conn;
};

}
