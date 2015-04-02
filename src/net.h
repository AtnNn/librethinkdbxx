#pragma once

#include <string>

#include "protocol_defs.h"

namespace RethinkDB {

class Connection {
public:
    Connection(const std::string&, int, const std::string&);

private:
    void recv(char*, size_t);
    std::string recv(size_t);
    size_t recv_cstring(char*, size_t);
    void send(const char*, size_t);
    void send(std::string);
    void close();

    const uint32_t version_magic = static_cast<uint32_t>(VersionDummy::Version::V0_4);
    const uint32_t json_magic = static_cast<uint32_t>(VersionDummy::Version::V0_4);

    int sockfd;
};

Connection connect(std::string, int, std::string);

}
