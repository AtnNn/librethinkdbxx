#include "testlib.h"

int failed = 0;
int count = 0;
std::vector<std::pair<const char*, bool>> section;

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

std::string to_string(const R::Error& error) {
    return "Error<" + error.message + ">";
}

std::string to_string(const R::Object& object) {
    return write_datum(object);
}

std::string to_string(const R::Array& array) {
    return write_datum(array);
}

std::string to_string(const R::Nil& nil) {
    return write_datum(nil);
}

bool equal(const R::Error &a, const R::Error& b) {
    return a.message == b.message;
}

bool equal(const char* a, const char* b);

void enter_section(const char* name) {
    section.emplace_back(name, true);
}

void exit_section() {
    section.pop_back();
}

std::string to_string(const err& error) {
    return "err(" + error.type + ", " +  error.message + ")";
}

bool equal(const R::Error& a, const err& b) {
    return a.message == b.message;
}

bool match(const char* pattern, const char* string) {
    return *R::expr(string).match(pattern).coerce_to("bool").run(*conn).get_boolean();
}

bool equal(const R::Error& a, const err_regex& b) {
    return match(b.message.c_str(), a.message.c_str());
}

std::string to_string(const err_regex& error) {
    return "err_regex(" + error.type + ", " +  error.message + ")";
}

R::Object partial(R::Object&& object) {
    object.emplace("<PARTIAL>", true);
    return object;
}

R::Datum uuid() {
    return R::Object{{"special", "uuid"}};
}

R::Object arrlen(int n, R::Datum&& datum) {
    return R::Object{{"special", "arrlen"},{"len",n},{"of",datum}};
}

R::Object arrlen(int n) {
    return R::Object{{"special", "arrlen"},{"len",n}};
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

R::Object bag(R::Array&& array) {
    return R::Object{{"special", "bag"}, {"bag", std::move(array)}};
};

struct {
    static std::string string_key(const R::Datum& datum) {
        const std::string* string = datum.get_string();
        if (string) return *string;
        return write_datum(datum);
    }

    bool operator() (const R::Object& want, const R::Datum& have) {
        if (want.count("special")) {
            // TODO
        } else if (want.count("$reql_type$")) {
            const std::string* type = want.find("$reql_type$")->second.get_string();
            if (type && *type == "GROUPED_DATA") {
                const R::Array* data = want.find("data")->second.get_array();
                R::Object object;
                for (R::Datum it : *data) {
                    object.emplace(string_key(it.extract_nth(0)), it.extract_nth(1)); 
                }
                return have == object;
            }
        }
        return have == want;
    }
    template <class T>
    bool operator() (T a, const R::Datum& b) {
        return b == a;
    }
} match_datum;

bool equal(const R::Datum& a, const R::Datum& b) {
    return b.apply<bool>(match_datum, a);
}
