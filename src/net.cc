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

const int debug_net = 0;

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
    if (debug_net > 1) fprintf(stderr, "<< %s\n", write_datum(std::string(buf, numbytes)).c_str());
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
        if (debug_net > 1) fprintf(stderr, ">> %s\n", write_datum(std::string(buf, numbytes)).c_str());
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
    if (it != conn->guarded_cache.end() && !it->second.closed) {
        send_query(token, write_datum(Datum(Array{Datum(static_cast<int>(Protocol::Query::QueryType::STOP))})));
    }
}

void Connection::ask_for_more(uint64_t token) {
    WriteLock writer(*this);
    writer.send_query(token, write_datum(Datum(Array{Datum(static_cast<int>(Protocol::Query::QueryType::CONTINUE))})));
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
            if (cache.closed && cache.responses.empty()) {
                guarded_cache.erase(token_want);
            }
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

        if (debug_net > 0) fprintf(stderr, "[%llu] << %s\n", token_got, write_datum(datum).c_str());

        Response response(std::move(datum));

        if (token_got == token_want) {
            guard.lock();
            if (response.type != Protocol::Response::ResponseType::SUCCESS_PARTIAL) {
                auto it = conn->guarded_cache.find(token_got);
                if (it != conn->guarded_cache.end()) {
                    it->second.closed = true;
                    it->second.cond.notify_all();
                }
                conn->guarded_cache.erase(it);
            }
            conn->guarded_loop_active = false;
            for (auto& it : conn->guarded_cache) {
                it.second.cond.notify_all();
            }
            return response;
        } else {
            guard.lock();
            auto it = conn->guarded_cache.find(token_got);
            if (it == conn->guarded_cache.end()) {
                // drop the response
            } else if (!it->second.closed) {
                it->second.responses.emplace(std::move(response));
                if (response.type != Protocol::Response::ResponseType::SUCCESS_PARTIAL) {
                    it->second.closed = true;
                }
            }
            it->second.cond.notify_all();
            guard.unlock();
        }
    }
}

Token Connection::start_query(const std::string& query) {
    WriteLock writer(*this);
    Token token(writer);
    writer.send_query(token.token, query);
    CacheLock guard(*this);
    guarded_cache[token.token];
    return token;
}

void Connection::WriteLock::send_query(uint64_t token, const std::string& query) {
    if (debug_net > 0) fprintf(stderr, "[%llu] >> %s\n", token, query.c_str());
    char buf[12];
    memcpy(buf, &token, 8);
    uint32_t size = query.size();
    memcpy(buf + 8, &size, 4);
    send(buf, 12);
    send(query);
}

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
    case RT::CLIENT_ERROR: err = "ReqlClientError"; break;
    case RT::COMPILE_ERROR: err = "ReqlCompileError"; break;
    case RT::RUNTIME_ERROR:
        switch (error_type) {
        case ET::RESOURCE_LIMIT: err = "ReqlResourceLimitError"; break;
        case ET::QUERY_LOGIC: err = "ReqlQueryLogicError"; break;
        case ET::NON_EXISTENCE: err = "ReqlNonExistenceError"; break;
        case ET::OP_FAILED: err = "ReqlOpFailedError"; break;
        case ET::OP_INDETERMINATE: err = "ReqlOpIndeterminateError"; break;
        case ET::USER: err = "ReqlUserError"; break;
        default: err = "runtime error"; break;
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

Token::Token(Connection::WriteLock& writer) : conn(writer.conn), token(writer.new_token()) { }

void Connection::close_token(uint64_t token) {
    WriteLock writer(*this);
    writer.close_token(token);
}

}
