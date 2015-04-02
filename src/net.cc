#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include <cstring>

#include "error.h"
#include "net.h"

namespace RethinkDB {

Connection connect(const std::string& host, int port, const std::string& auth_key) {
    return Connection(host, port, auth_key);
}

Connection::Connection(const std::string& host, int port, const std::string& auth_key) {
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
    if (len == max_response_length || !strcmp(buf, "SUCCESS")) {
        buf[len] = 0;
        try {
            close();
        } catch (...) { }
        throw Error("Server rejected connection with message: %s", buf);
    }
}

void Connection::recv(char* buf, size_t size) {
    while (size) {
        ssize_t numbytes = ::recv(sockfd, buf, size, 0);
        if (numbytes == -1) throw Error::from_errno("recv");
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
    int ret = ::close(sockfd);
    if (ret == -1) {
        throw Error::from_errno("close");
    }
}

}
