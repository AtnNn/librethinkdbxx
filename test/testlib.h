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

struct err {
    err(const char* type_, const char* message_, R::Array&& backtrace_ = {}) :
        type(type_), message(message_), backtrace(std::move(backtrace_)) { }
    std::string type;
    std::string message;
    R::Array backtrace;
};

struct err_regex {
    err_regex(const char* type_, const char* message_, R::Array&& backtrace_) :
        type(type_), message(message_), backtrace(std::move(backtrace_)) { }
    std::string type;
    std::string message;
    R::Array backtrace;
};

R::Object partial(R::Object&& object) {
    object.emplace("<PARTIAL>", true);
    return object;
}

R::Datum uuid() {
    return "<UUID>";
}

R::Datum arrlen(int n, R::Datum&& datum) {
    R::Array array;
    for (int i = 0; i < n; ++i) {
        array.emplace_back(std::move(datum));
    }
    return array;
}

R::Query new_table() {
    char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    char name[10];
    for (unsigned int i = 0; i + 1 < sizeof name; ++i) {
        name[i] = chars[random() % sizeof chars];
    }
    name[9] = 0;
    R::table_create(name).run(*conn);
    return R::expr(name);
}

std::string repeat(std::string&& s, int n) {
    std::string string;
    string.reserve(n * s.size());
    for (int i = 0; i < n; ++i) {
        string.append(s);
    }
    return string;
}

R::Query fetch(R::Cursor& cursor, int count) {
    R::Array array;
    for (int i = 0; i < count; ++i) {
        array.emplace_back(cursor.next());
    }
    return expr(std::move(array));
}

struct bag {
    bag(R::Array array_) : array(array_) {}
    R::Array array;
};
