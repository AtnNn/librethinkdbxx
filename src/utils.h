#pragma once

#include <cstddef>
#include <string>

namespace RethinkDB {

const size_t max_utf8_encoded_size = 6;

bool base64_decode(const std::string&, std::string&);
std::string base64_encode(const std::string&);

size_t utf8_encode(unsigned int, char*);

}
