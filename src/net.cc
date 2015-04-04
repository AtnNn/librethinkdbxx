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

class Connection::ReadLock {
public:
    ReadLock(Connection& conn) : lock(conn.read_lock), conn(&conn) { }

    size_t recv_some(char*, size_t);
    void recv(char*, size_t);
    std::string recv(size_t);
    size_t recv_cstring(char*, size_t);

    Response read_loop(uint64_t, CacheLock&&);

    std::lock_guard<std::mutex> lock;
    Connection* conn;
};

class Connection::WriteLock {
public:
    WriteLock(Connection& conn) : lock(conn.write_lock), conn(&conn) { }

    void send(const char*, size_t);
    void send(std::string);
    uint64_t new_token();
    void close();
    void close_token(uint64_t);
    void send_query(uint64_t token, const std::string& query);

    std::lock_guard<std::mutex> lock;
    Connection* conn;
};

class Connection::CacheLock {
public:
    CacheLock(Connection& conn) : inner_lock(conn.cache_lock) { }

    void lock() {
        inner_lock.lock();
    }

    void unlock() {
        inner_lock.unlock();
    }

    std::unique_lock<std::mutex> inner_lock;
};

std::unique_ptr<Connection> connect(std::string host, int port, std::string auth_key) {
    return std::unique_ptr<Connection>(new Connection(host, port, auth_key));
}

Connection::Connection(const std::string& host, int port, const std::string& auth_key) : guarded_next_token(1) {
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
        guarded_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (guarded_sockfd == -1) {
            error = Error::from_errno("socket");
            continue;
        }

        if (connect(guarded_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            ::close(guarded_sockfd);
            error = Error::from_errno("connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        throw error;
    }

    freeaddrinfo(servinfo);

    WriteLock writer(*this);

    {
        size_t size = auth_key.size();
        char buf[12 + size];
        memcpy(buf, &version_magic, 4);
        uint32_t n = size;
        memcpy(buf + 4, &n, 4);
        memcpy(buf + 8, auth_key.data(), size);
        memcpy(buf + 8 + size, &json_magic, 4);
        writer.send(buf, sizeof buf);
    }
    
    ReadLock reader(*this);

    const size_t max_response_length = 1024;
    char buf[max_response_length + 1];
    size_t len = reader.recv_cstring(buf, max_response_length);
    if (len == max_response_length || strcmp(buf, "SUCCESS")) {
        buf[len] = 0;
        try {
            close();
        } catch (...) { }
        throw Error("Server rejected connection with message: %s", buf);
    }
}

size_t Connection::ReadLock::recv_some(char* buf, size_t size) {
    ssize_t numbytes = ::recv(conn->guarded_sockfd, buf, size, 0);
    if (numbytes == -1) throw Error::from_errno("recv");
    return numbytes;
}

void Connection::ReadLock::recv(char* buf, size_t size) {
    while (size) {
        size_t numbytes = recv_some(buf, size);
        if (numbytes == 0) throw Error("Lost connection to remote server");
        buf += numbytes;
        size -= numbytes;
    }
}

size_t Connection::ReadLock::recv_cstring(char* buf, size_t max_size){
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

void Connection::WriteLock::send(const char* buf, size_t size) {
    while (size) {
        ssize_t numbytes = ::write(conn->guarded_sockfd, buf, size);
        if (numbytes == -1) throw Error::from_errno("write");
        buf += numbytes;
        size -= numbytes;
    }
}

void Connection::WriteLock::send(const std::string data) {
    send(data.data(), data.size());
}

std::string Connection::ReadLock::recv(size_t size) {
    char buf[size];
    recv(buf, size);
    return buf;
}

void Connection::close() {
    Connection::WriteLock writer(*this);
    CacheLock guard(*this);

    for (auto& it : guarded_cache) {
        writer.close_token(it.first);
    }

    int ret = ::close(guarded_sockfd);
    if (ret == -1) {
        throw Error::from_errno("close");
    }
}

uint64_t Connection::WriteLock::new_token() {
    return conn->guarded_next_token++;
}

void Connection::WriteLock::close_token(uint64_t token) {
    CacheLock guard(*conn);
    const auto& it = conn->guarded_cache.find(token);
    if (it != conn->guarded_cache.end()) {
        it->second.closed = true;
        it->second.cond.notify_all();
        conn->guarded_cache.erase(it);
    }
    send_query(token, write_datum(Datum(Array{Datum(static_cast<int>(Protocol::Query::QueryType::STOP))})));
}

class ResponseBuffer : public BufferedInputStream {
public:
    ResponseBuffer(Connection::ReadLock* reader_, uint32_t length)
        : reader(reader_), remaining(length) { }
    size_t read_chunk(char *buf, size_t size) {
        size_t n = reader->recv_some(buf, std::min(size, remaining));
        remaining -= n;
        return n;
    }
private:
    Connection::ReadLock* reader;
    size_t remaining;
};

Response Connection::wait_for_response(uint64_t token_want) {
    CacheLock guard(*this);

    TokenCache& cache = guarded_cache[token_want];

    while (true) {
        if (!cache.responses.empty()) {
            Response response(std::move(cache.responses.front()));
            cache.responses.pop();
            return response;
        }

        if (cache.closed) {
            throw Error("Trying to read from a closed token");
        }

        if (guarded_loop_active) {
            cache.cond.wait(guard.inner_lock);
        } else {
            break;
        } 
    }

    ReadLock reader(*this);
    return reader.read_loop(token_want, std::move(guard));
}

Response Connection::ReadLock::read_loop(uint64_t token_want, CacheLock&& guard) {
    if (!guard.inner_lock) {
        guard.lock();
    }
    if (conn->guarded_loop_active) {
        throw Error("Cannot run more than one read loop on the same connection");
    }
    conn->guarded_loop_active = true;
    guard.unlock();

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
            guard.lock();
            conn->guarded_loop_active = false;
            for (auto& it : conn->guarded_cache) {
                it.second.cond.notify_all();
            }
            return response;
        } else {
            guard.lock();
            TokenCache& cache = conn->guarded_cache[token_got];
            cache.responses.emplace(std::move(datum));
            cache.cond.notify_all();
            guard.unlock();
        }
    }
}

Token Connection::start_query(const std::string& query) {
    WriteLock writer(*this);
    Token token(writer);
    writer.send_query(token.token, query);
    return token;
}

void Connection::WriteLock::send_query(uint64_t token, const std::string& query) {
    char buf[12];
    memcpy(buf, &token, 8);
    uint32_t size = query.size();
    memcpy(buf + 8, &size, 4);
    send(buf, 12);
    send(query);
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

Token::Token(Connection::WriteLock& writer) : conn(writer.conn), token(writer.new_token()) { }

void Connection::close_token(uint64_t token) {
    WriteLock writer(*this);
    writer.close_token(token);
}

}
