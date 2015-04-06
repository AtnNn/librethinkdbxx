#pragma once

#include <sstream>
#include <cstdio>
#include <stack>

#include <rethinkdb.h>

namespace R = RethinkDB;

extern std::vector<std::pair<const char*, bool>> section;
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

std::string to_string(const R::Error& error);
std::string to_string(const R::Object& object);
std::string to_string(const R::Array& array);
std::string to_string(const R::Nil& nil);

bool equal(const R::Datum& a, const R::Datum& b);

template <class T>
bool equal(T a, const R::Error& b) {
    return false;
}

template <class T>
bool equal(const R::Error& a, T b) {
    return false;
}

bool equal(const R::Error &a, const R::Error& b);
bool equal(const char* a, const char* b);

template <class T, class U>
void test_eq(const char* code, T&& val, U&& expected) {
    count ++;
    if (!equal(val, expected)) {
        const char spaces[] = "                           ";
        failed++;
        const char* indent = spaces + sizeof(spaces) - 1;
        for (auto& it : section) {
            if (it.second) {
                printf("%sSection: %s\n", indent, it.first);
                it.second = false;
            }
            indent -= 2;
        }
        printf("%sFAILURE: in `%s':\n%s  Expected: `%s'\n%s   but got: `%s'\n",
               indent, code,
               indent, to_string(expected).c_str(),
               indent, to_string(val).c_str());
    }
}

void enter_section(const char* name);
void exit_section();

#define TEST_EQ(code, expected) \
    do {                                                                \
        try { test_eq(#code, (code), (expected)); }                     \
        catch (const R::Error& error) { test_eq(#code, error, (expected)); } \
    } while (0)

struct err {
    err(const char* type_, const char* message_, R::Array&& backtrace_ = {}) :
        type(type_), message(message_), backtrace(std::move(backtrace_)) { }

    std::string convert_type() const {
        if (type == "RqlRuntimeError") {
            return "runtime error";
        }
        return type;
    }

    static std::string trim_message(std::string msg) {
        int i = msg.find(":\n");
        if (i != std::string::npos) {
            return msg.substr(0, i) + ".";
        }
        return msg;
    }

    std::string type;
    std::string message;
    R::Array backtrace;
};

std::string to_string(const err& error);

bool equal(const R::Error& a, const err& b);

template <class T>
bool equal(T a, const err& b) {
    return false;
}

struct err_regex {
    err_regex(const char* type_, const char* message_, R::Array&& backtrace_) :
        type(type_), message(message_), backtrace(std::move(backtrace_)) { }
    std::string type;
    std::string message;
    R::Array backtrace;
};

bool match(const char* pattern, const char* string);

bool equal(const R::Error& a, const err_regex& b);

template <class T>
bool equal(T a, const err_regex& b) {
    return false;
}

std::string to_string(const err_regex& error);

R::Object partial(R::Object&& object);

R::Datum uuid();

R::Object arrlen(int n, R::Datum&& datum);

R::Object arrlen(int n);

R::Query new_table();

std::string repeat(std::string&& s, int n);

R::Query fetch(R::Cursor& cursor, int count);

R::Object bag(R::Array&& array);

struct temp_table {
    temp_table() {
        char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        char name_[15] = "temp_";
        for (unsigned int i = 5; i + 1 < sizeof name_; ++i) {
            name_[i] = chars[random() % sizeof chars];
        }
        name_[14] = 0;
        R::table_create(name_).run(*conn);
        name = name_; 
    }
    ~temp_table() { R::table_drop(name).run(*conn); }
    R::Query table() { return R::table(name); }
    std::string name;
};

bool equal(const R::Datum& got, const R::Object& expected);
