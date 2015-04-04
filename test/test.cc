#include <signal.h>

#include <sstream>
#include <cstdio>

#include <rethinkdb.h>

namespace R = RethinkDB;

int failed = 0;
int count = 0;

std::unique_ptr<R::Connection> conn;

std::string to_string(const char* string) {
    return string;
}

std::string to_string(const R::Datum& datum) {
    return write_datum(datum);
}

template <class T>
std::string to_string(T a) {
    std::ostringstream s;
    s << a;
    return s.str();
}

template <class T, class U>
bool equal(T a, U b) {
    return a == b;
}

bool equal(const char* a, const char* b) {
    return !strcmp(a, b);
}

template <class T, class U>
void test_eq(T&& val, U&& expected) {
    count ++;
    if (!equal(val, expected)) {
        failed = true;
        printf("FAILURE: Expected `%s' but got `%s'\n",
               to_string(expected).c_str(),
               to_string(val).c_str());
    }
}

void test_json(const char* string, const char* ret = "") {
    test_eq(R::write_datum(R::read_datum(string)).c_str(), ret[0] ? ret : string);
}

void test_json_parse_print() {
    test_json("null");
    test_json("1.2");
    test_json("1.2e20", "1.2e+20");
    test_json("true");
    test_json("false");
    test_json("\"\"");
    test_json("\"\\u1234\"", "\"\u1234\"");
    test_json("\"\\\"\"");
    test_json("\"foobar\"");
    test_json("[]");
    test_json("[1]");
    test_json("[1,2,3,4]");
    test_json("{}");
    test_json("{\"a\":1}");
    test_json("{\"a\":1,\"b\":2,\"c\":3}");
}

void test_reql() {
    test_eq((R::expr(1) + 2).run(*conn), R::Datum(3));
    test_eq(R::range(4).count().run(*conn), R::Datum(4));
}

void test_cursor() {
    R::Cursor cursor = R::range(10000).run_cursor(*conn);
    test_eq(cursor.next(), 0);
    R::Array array = cursor.toArray();
    test_eq(array.size(), 9999);
    test_eq(*array.begin(), 1);
    test_eq(*array.rbegin(), 9999);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    try {
        conn = R::connect();
    } catch(const R::Error& error) {
        printf("FAILURE: could not connect to localhost:28015: %s\n", error.message.c_str());
        return 1;
    }
    try {
        test_json_parse_print();
        test_reql();
        test_cursor();
    } catch (const R::Error& error) {
        printf("FAILURE: uncaught expception: %s\n", error.message.c_str());
        return 1;
    }
    if (!failed) {
        printf("SUCCESS: All %d tests passed\n", count);
    } else {
        return 1;
    }
}
