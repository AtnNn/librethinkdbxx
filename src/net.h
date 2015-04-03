#pragma once

#include <string>

#include "protocol_defs.h"

namespace RethinkDB {

class Connection {
public:
    Connection(const std::string&, int, const std::string&);
    uint64_t new_token();

private:
    size_t recv_some(char* buf, size_t size);
    void recv(char*, size_t);
    std::string recv(size_t);
    size_t recv_cstring(char*, size_t);
    void send(const char*, size_t);
    void send(std::string);
    void close();

    const uint32_t version_magic = static_cast<uint32_t>(Protocol::VersionDummy::Version::V0_4);
    const uint32_t json_magic = static_cast<uint32_t>(Protocol::VersionDummy::Version::V0_4);

    uint64_t next_token;
    int sockfd;
};

Connection connect(std::string, int, std::string);

}
