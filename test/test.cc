#include <cstdio>

#include <rethinkdb.h>

namespace R = RethinkDB;

R::OutputFileDescriptor out(1);

void test_json(const char* string) {
    printf("Round-trip %s: ", string);
    fflush(stdout);
    R::write_datum(R::read_datum(string), out);
    puts("");
}

int main() {
    try {
        test_json("null");
        test_json("1.2");
        test_json("1.2e20");
        test_json("true");
        test_json("false");
        test_json("\"\"");
        test_json("\"\u1234\"");
        test_json("\"\\\"\"");
        test_json("\"foobar\"");
        test_json("[]");
        test_json("[1]");
        test_json("[1,2,3,4]");
        test_json("{}");
        test_json("{\"a\":1}");
        test_json("{\"a\":1,\"b\":2,\"c\":3}");
    } catch (const R::Error& error) {
        printf("Tests failed: %s\n", error.message.c_str());
    }
}
