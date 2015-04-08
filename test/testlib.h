#pragma once

#include <sstream>
#include <cstdio>
#include <stack>
#include <cmath>

#include <rethinkdb.h>

namespace R = RethinkDB;

extern std::vector<std::pair<const char*, bool>> section;
extern int failed;
extern int count;
extern std::unique_ptr<R::Connection> conn;
extern int verbosity;

const char* indent();

void enter_section(const char* name);
void exit_section();

#define TEST_DO(code)                                                   \
    if (verbosity > 1) fprintf(stderr, "%sTEST: %s\n", indent(), #code); \
    code

#define TEST_EQ(code, expected) \
    do {                                                                \
        if (verbosity > 1) fprintf(stderr, "%sTEST: %s\n", indent(), #code); \
        try { test_eq(#code, (code), (expected)); }                     \
        catch (const R::Error& error) { test_eq(#code, error, (expected)); } \
    } while (0)

struct err {
    err(const char* type_, std::string message_, R::Array&& backtrace_ = {}) :
        type(type_), message(message_), backtrace(std::move(backtrace_)) { }

    std::string convert_type() const {
        if (type == "RqlRuntimeError") {
            return "runtime error";
        }
        if (type == "RqlCompileError") {
            return "compile error";
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

struct err_regex {
    err_regex(const char* type_, const char* message_, R::Array&& backtrace_ = {}) :
        type(type_), message(message_), backtrace(std::move(backtrace_)) { }
    std::string type;
    std::string message;
    R::Array backtrace;
};

bool match(const char* pattern, const char* string);


R::Object partial(R::Object&& object);
R::Object partial(R::Array&& object);
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

void clean_slate();

std::string to_string(const R::Datum&);
std::string to_string(const R::Error&);
std::string to_string(const err_regex&);
std::string to_string(const err&);

bool equal(const R::Datum&, const R::Datum&);
bool equal(const R::Error&, const err_regex&);
bool equal(const R::Error&, const err&);

template <class T>
bool equal(const T& a, const err& b) {
    return false;
}

template <class T>
bool equal(const T& a, const err_regex& b) {
    return false;
}

template <class T>
bool equal(const R::Error& a, const T& b) {
    return false;
}

std::string truncate(std::string&&);

template <class T, class U>
void test_eq(const char* code, const T val, const U expected) {
    count ++;
    if (!equal(val, expected)) {
        failed++;
        for (auto& it : section) {
            if (it.second) {
                printf("%sSection: %s\n", indent(), it.first);
                it.second = false;
            }
        }
        printf("%sFAILURE in ‘%s’:\n%s  Expected: ‘%s’\n%s   but got: ‘%s’\n",
               indent(), code,
               indent(), truncate(to_string(expected)).c_str(),
               indent(), truncate(to_string(val)).c_str());
    }
}

#define PacificTimeZone() (-7 * 3600)
#define UTCTimeZone() (0)
