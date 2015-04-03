#pragma once

#include <string>

#include "protocol_defs.h"
#include "datum.h"
#include "error.h"

namespace RethinkDB {

class Token;
class Connection;

Protocol::Response::ResponseType response_type(double t) {
    int n = static_cast<int>(t);
    switch (n) {
    case static_cast<int>(Protocol::Response::ResponseType::SUCCESS_ATOM):
        return Protocol::Response::ResponseType::SUCCESS_ATOM;
    case static_cast<int>(Protocol::Response::ResponseType::SUCCESS_SEQUENCE):
        return Protocol::Response::ResponseType::SUCCESS_SEQUENCE;
    case static_cast<int>(Protocol::Response::ResponseType::SUCCESS_PARTIAL):
        return Protocol::Response::ResponseType::SUCCESS_PARTIAL;
    case static_cast<int>(Protocol::Response::ResponseType::WAIT_COMPLETE):
        return Protocol::Response::ResponseType::WAIT_COMPLETE;
    case static_cast<int>(Protocol::Response::ResponseType::CLIENT_ERROR):
        return Protocol::Response::ResponseType::CLIENT_ERROR;
    case static_cast<int>(Protocol::Response::ResponseType::COMPILE_ERROR):
        return Protocol::Response::ResponseType::COMPILE_ERROR;
    case static_cast<int>(Protocol::Response::ResponseType::RUNTIME_ERROR):
        return Protocol::Response::ResponseType::RUNTIME_ERROR;
    default:
        throw Error("Unknown response type");
    }
}

class Response {
    Response(Datum&& datum) :
        type(response_type(std::move(datum).get_field("t").get_double())),
        result(std::move(datum).get_field("r").get_array()) { }
    Protocol::Response::ResponseType type;
    Array result;
};

class Connection {
public:
    Connection(const std::string&, int, const std::string&);

    Connection(const Connection&) = delete;
    Connection(Connection&&) = default;

    Token start_query(std::string);
    void close();

private:
    size_t recv_some(char* buf, size_t size);
    void recv(char*, size_t);
    std::string recv(size_t);
    size_t recv_cstring(char*, size_t);
    void send(const char*, size_t);
    void send(std::string);

    const uint32_t version_magic = static_cast<uint32_t>(Protocol::VersionDummy::Version::V0_4);
    const uint32_t json_magic = static_cast<uint32_t>(Protocol::VersionDummy::Version::V0_4);

    friend class Token;
    uint64_t new_token();
    void close_token(uint64_t);
    Response wait_for_response(uint64_t); 

    // TODO std::map 
    uint64_t next_token;
    int sockfd;
};

Connection connect(std::string, int, std::string);

class Token {
public:
    Token(Connection& conn_) : conn(&conn_), token(conn->new_token()) { }

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
    Connection* conn;
    uint64_t token;
};

}
