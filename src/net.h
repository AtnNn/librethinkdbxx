#pragma once

#include <string>
#include <queue>

#include "protocol_defs.h"
#include "datum.h"
#include "error.h"

namespace RethinkDB {

class Token;
class Connection;
class ResponseBuffer;

Protocol::Response::ResponseType response_type(double t);

class Response {
public:
    Response(Datum&& datum) :
        type(response_type(std::move(datum).get_field("t").get_double())),
        result(std::move(datum).get_field("r").get_array()) { }
    Error as_error();
    Protocol::Response::ResponseType type;
    Array result;
};

class Connection {
public:
    Connection(const std::string&, int, const std::string&);

    Connection(const Connection&) = delete;
    Connection(Connection&&) = default;

    Token send_query(std::string);
    void close();

private:
    friend class ResponseBuffer;

    size_t recv_some(char* buf, size_t size);
    void recv(char*, size_t);
    std::string recv(size_t);
    size_t recv_cstring(char*, size_t);
    void send(const char*, size_t);
    void send(std::string);

    const uint32_t version_magic = static_cast<uint32_t>(Protocol::VersionDummy::Version::V0_4);
    const uint32_t json_magic = static_cast<uint32_t>(Protocol::VersionDummy::Protocol::JSON);

    friend class Token;
    uint64_t new_token();
    void close_token(uint64_t);
    Response wait_for_response(uint64_t); 

    std::map<uint32_t, std::queue<Datum>> cache;
    uint64_t next_token;
    int sockfd;
};

Connection connect(std::string host = "localhost", int port = 28015, std::string auth_key = "");

class Token {
public:
    Token(Connection* conn_) : conn(conn_), token(conn_->new_token()) { }

    Token(const Token&) = delete;
    Token(Token&& other) : conn(other.conn), token(other.token) {
        other.conn = NULL;
    }

    Response wait_for_response() {
        return conn->wait_for_response(token);
    }

    void close() {
        if(conn) {
            conn->close_token(token);
        }
    }

    ~Token() {
        close();
    }
private:
    friend class Connection;
    Connection* conn;
    uint64_t token;
};

}
