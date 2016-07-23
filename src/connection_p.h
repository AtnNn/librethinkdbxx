#ifndef CONNECTION_P_H
#define CONNECTION_P_H

#include "connection.h"

namespace RethinkDB {

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

class Token;
class ConnectionPrivate {
public:
    ConnectionPrivate(Connection *conn_) : guarded_next_token(1), conn(conn_) {}
    Token start_query(const std::string& query);
    Response wait_for_response(uint64_t, double);

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

    Connection * const conn;
};

class CacheLock {
public:
    CacheLock(ConnectionPrivate& conn) : inner_lock(conn.cache_lock) { }

    void lock() {
        inner_lock.lock();
    }

    void unlock() {
        inner_lock.unlock();
    }

    std::unique_lock<std::mutex> inner_lock;
};

class ReadLock {
public:
    ReadLock(ConnectionPrivate& conn) : lock(conn.read_lock), conn(&conn) { }

    size_t recv_some(char*, size_t, double wait);
    void recv(char*, size_t, double wait);
    std::string recv(size_t);
    size_t recv_cstring(char*, size_t);

    Response read_loop(uint64_t, CacheLock&&, double);

    std::lock_guard<std::mutex> lock;
    ConnectionPrivate* conn;
};

class WriteLock {
public:
    WriteLock(ConnectionPrivate& conn) : lock(conn.write_lock), conn(&conn) { }

    void send(const char*, size_t);
    void send(std::string);
    uint64_t new_token();
    void close();
    void close_token(uint64_t);
    void send_query(uint64_t token, const std::string& query);

    std::lock_guard<std::mutex> lock;
    ConnectionPrivate* conn;
};

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
    friend class ConnectionPrivate;

    uint64_t token;
    Connection* conn;
};

}   // namespace RethinkDB

#endif  // CONNECTION_P_H
