#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "error.h"
#include "net.h"
#include "stream.h"
#include "json.h"

namespace RethinkDB {

Connection connect(std::string host, int port, std::string auth_key) {
    return Connection(host, port, auth_key);
}

Connection::Connection(const std::string& host, int port, const std::string& auth_key) : next_token(1) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    char port_str[16];
    snprintf(port_str, 16, "%d", port);
    struct addrinfo *servinfo;
    int ret = getaddrinfo(host.c_str(), port_str, &hints, &servinfo);
    if (ret) throw Error("getaddrinfo: %s\n", gai_strerror(ret));

    struct addrinfo *p;
    Error error;
    for(p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            error = Error::from_errno("socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            ::close(sockfd);
            error = Error::from_errno("connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        throw error;
    }

    freeaddrinfo(servinfo);

    {
        size_t size = auth_key.size();
        char buf[12 + size];
        memcpy(buf, &version_magic, 4);
        uint32_t n = size;
        memcpy(buf + 4, &n, 4);
        memcpy(buf + 8, auth_key.data(), size);
        memcpy(buf + 8 + size, &json_magic, 4);
        send(buf, sizeof buf);
    }
    
    const size_t max_response_length = 1024;
    char buf[max_response_length + 1];
    size_t len = recv_cstring(buf, max_response_length);
    if (len == max_response_length || strcmp(buf, "SUCCESS")) {
        buf[len] = 0;
        try {
            close();
        } catch (...) { }
        throw Error("Server rejected connection with message: %s", buf);
    }
}

size_t Connection::recv_some(char* buf, size_t size) {
    ssize_t numbytes = ::recv(sockfd, buf, size, 0);
    if (numbytes == -1) throw Error::from_errno("recv");
    return numbytes;
}

void Connection::recv(char* buf, size_t size) {
    while (size) {
        size_t numbytes = recv_some(buf, size);
        if (numbytes == 0) throw Error("Lost connection to remote server");
        buf += numbytes;
        size -= numbytes;
    }
}

size_t Connection::recv_cstring(char* buf, size_t max_size){
    size_t size = 0;
    for (; size < max_size; size++) {
        recv(buf, 1);
        if (*buf == 0) {
            break;
        }
        buf++;
    }
    return size;
}

void Connection::send(const char* buf, size_t size) {
    while (size) {
        ssize_t numbytes = ::write(sockfd, buf, size);
        if (numbytes == -1) throw Error::from_errno("write");
        buf += numbytes;
        size -= numbytes;
    }
}

void Connection::send(const std::string data) {
    send(data.data(), data.size());
}

std::string Connection::recv(size_t size) {
    char buf[size];
    recv(buf, size);
    return buf;
}

void Connection::close() {
    // TODO

    int ret = ::close(sockfd);
    if (ret == -1) {
        throw Error::from_errno("close");
    }
}

uint64_t Connection::new_token() {
    return next_token++;
}

void Connection::close_token(uint64_t) {
    // TODO
}

class ResponseBuffer : public BufferedInputStream {
public:
    ResponseBuffer(Connection* conn_, uint32_t length)
        : conn(conn_), remaining(length) { }
    size_t read_chunk(char *buf, size_t size) {
        size_t n = conn->recv_some(buf, std::min(size - 1 /* TODO */, remaining));
        buf[n] = 0;
        remaining -= n;
        return n;
    }
private:
    Connection* conn;
    size_t remaining;
};

Response Connection::wait_for_response(uint64_t token_want) {
    auto it = cache.find(token_want);
    if (it != cache.end()) {
        if (!it->second.empty()) {
            Response response(std::move(it->second.front()));
            it->second.pop();
            return response;
        }
    }
    while (true) {
        char buf[12];
        recv(buf, 12);
        uint64_t token_got;
        memcpy(&token_got, buf, 8);
        uint32_t length;
        memcpy(&length, buf + 8, 4);
        ResponseBuffer stream(this, length);
        Datum datum = read_datum(stream);
        if (token_got == token_want) {
            Response response(std::move(datum));
            return response;
        } else {
            auto it = cache.find(token_got);
            if (it != cache.end()) {
                it->second.emplace(std::move(datum));
            }
        }
    }
}

Token Connection::send_query(std::string query) {
    Token token(this);
    char buf[12];
    memcpy(buf, &token.token, 8);
    uint32_t size = query.size();
    memcpy(buf + 8, &size, 4);
    send(buf, 12);
    send(query);
    return token;
}

Error Response::as_error() {
    std::string repr = write_datum(Datum(result));
    return Error("Invalid response type %d: %s", static_cast<int>(type), repr.c_str());
}

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

}
