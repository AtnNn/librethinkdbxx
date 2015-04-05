#pragma once

#include <sstream>
#include <cstdio>
#include <stack>

#include <rethinkdb.h>

namespace R = RethinkDB;

std::stack<const char*> section;
extern int failed;
extern int count;
extern std::unique_ptr<R::Connection> conn;

std::string to_string(const char*);
std::string to_string(const R::Datum&);

template <class T>
std::string to_string(T a) {
    std::ostringstream s;
    s << a;
    return s.str();
}

std::string to_string(R::Error error) {
    return "Error<" + error.message + ">";
}

template <class T, class U>
bool equal(T a, U b) {
    return a == b;
}

template <class T>
bool equal(T a, const R::Error& b) {
    return false;
}

template <class T>
bool equal(const R::Error& a, T b) {
    return false;
}

bool equal(const R::Error &a, const R::Error& b) {
    return a.message == b.message;
}

bool equal(const char* a, const char* b);

template <class T, class U>
void test_eq(const char* code, T&& val, U&& expected) {
    count ++;
    if (!equal(val, expected)) {
        failed = true;
        printf("FAILURE: Expected `%s' but got `%s' in `%s'\n",
               to_string(expected).c_str(),
               to_string(val).c_str(),
               code);
    }
}

void enter_section(const char* name) {
    section.push(name);
}

void exit_section() {
    section.pop();
}

#define TEST_EQ(code, expected) \
    do {                                                                \
        try { test_eq(#code, (code), (expected)); }                     \
        catch (const R::Error& error) { test_eq(#code, error, (expected)); } \
    } while (0)

R::Error err(const char* type, const char* message, R::Datum backtrace) {
    return R::Error(message);
}
