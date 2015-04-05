#include "testlib.h"

int failed = 0;
int count = 0;

std::unique_ptr<R::Connection> conn;

std::string to_string(const char* string) {
    return string;
}

std::string to_string(const R::Datum& datum) {
    return write_datum(datum);
}

bool equal(const char* a, const char* b) {
    return !strcmp(a, b);
}
