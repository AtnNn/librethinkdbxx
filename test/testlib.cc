#include <algorithm>

#include "testlib.h"

int verbosity = 2;

int failed = 0;
int count = 0;
std::vector<std::pair<const char*, bool>> section;

std::unique_ptr<R::Connection> conn;

// std::string to_string(const R::Cursor&) {
//     return "<Cursor>";
// }

std::string to_string(const R::Query& query) {
    return to_string(query.get_datum());
}

std::string to_string(const R::Datum& datum) {
    return write_datum(datum);
}

std::string to_string(const R::Error& error) {
    return "Error(\"" + error.message + "\")";
}

void enter_section(const char* name) {
    if (verbosity == 0) {
        section.emplace_back(name, true);
    } else {
        printf("%sSection %s\n", indent(), name);
        section.emplace_back(name, false);
    }
}

void exit_section() {
    section.pop_back();
}

std::string to_string(const err& error) {
    return "Error(\"" + error.convert_type() + ": " +  error.message + "\")";
}

bool equal(const R::Error& a, const err& b) {
    return b.trim_message(a.message) == (b.convert_type() + ": " + b.message);
}

bool match(const char* pattern, const char* string) {
    try {
        return !R::expr(string).match(pattern).run(*conn).to_datum().is_nil();
    } catch (const R::Error&) {
        return false;
    }
}

bool equal(const R::Error& a, const err_regex& b) {
    if (b.message == "Object keys must be strings" &&
        a.message == "runtime error: Expected type STRING but found NUMBER.") {
        return true;
    }
    return match(b.message.c_str(), a.message.c_str());
}

std::string to_string(const err_regex& error) {
    return "err_regex(" + error.type + ", " +  error.message + ")";
}

R::Object partial(R::Object&& object) {
    return R::Object{{"special", "partial"}, {"partial", std::move(object)}};
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

R::Query fetch(R::Cursor& cursor, int count, double timeout) {
    printf("fetch(..., %d, %lf)\n", count, timeout);
    R::Array array;
    int deadline = time(NULL) + int(timeout);
    for (int i = 0; count == -1 || i < count; ++i) {
        printf("fetching next (%d)\n", i);
        if (time(NULL) > deadline) break;

        try {
            array.emplace_back(cursor.next());
            printf("got %s\n", write_datum(array[array.size()-1]).c_str());
        } catch (const R::Error &e) {
            if (e.message != "next: No more data") {
                throw e;    // rethrow
            }

            break;
        }
    }

    return expr(std::move(array));
}

R::Object bag(R::Array&& array) {
    return R::Object{{"special", "bag"}, {"bag", std::move(array)}};
};

R::Object bag(R::Datum&& d) {
    return R::Object{{"special", "bag"}, {"bag", std::move(d)}};
};

std::string string_key(const R::Datum& datum) {
    const std::string* string = datum.get_string();
    if (string) return *string;
    return write_datum(datum);
}

bool falsey(R::Datum&& datum) {
    bool* boolean = datum.get_boolean();
    if (boolean) return !*boolean;
    double* number = datum.get_number();
    if (number) return *number == 0;
    return false;
}

bool equal(const R::Datum& got, const R::Datum& expected) {
    const std::string* string = expected.get_string();
    if (string) {
        const R::Binary* binary = got.get_binary();
        if (binary) {
            return *binary == R::Binary(*string);
        }
    }
    if (expected.get_object() && expected.get_field("$reql_type$")) {
        if (!got.get_field("$reql_type$")) {
            R::Datum datum = got.to_raw();
            if (datum.get_field("$reql_type$")) {
                return equal(datum, expected);
            }
        }
    }
    if (got.get_object() && got.get_field("$reql_type$")) {
        const std::string* type = got.get_field("$reql_type$")->get_string();
        if (type && *type == "GROUPED_DATA" &&
            (!expected.get_object() || !expected.get_field("$reql_type$"))) {
            const R::Array* data = got.get_field("data")->get_array();
            R::Object object;
            for (R::Datum it : *data) {
                object.emplace(string_key(it.extract_nth(0)), it.extract_nth(1));
            }
            return equal(object, expected);
        }
    }
    do {
        if (!expected.get_object()) break;
        if(!expected.get_field("special")) break;
        const std::string* type = expected.get_field("special")->get_string();
        if (!type) break;
        if (*type == "bag") {
            const R::Datum* bag_datum = expected.get_field("bag");
            if (!bag_datum || !bag_datum->get_array()) break;
            R::Array bag = *bag_datum->get_array();
            const R::Array* array = got.get_array();
            if (!array) return false;
            if (bag.size() != array->size()) return false;
            for (const auto& it : *array) {
                auto ref = std::find(bag.begin(), bag.end(), it);
                if (ref == bag.end()) return false;
                bag.erase(ref);
            }
            return true;
        } else if (*type == "arrlen") {
            const R::Datum* len_datum = expected.get_field("len");
            if (!len_datum) break;
            const double *len = len_datum->get_number();
            if (!len) break;
            const R::Array* array = got.get_array();
            if (!array) break;
            return array->size() == *len;
        } else if (*type == "partial") {
            const R::Object* object = got.get_object();
            if (object) {
                const R::Datum* partial_datum = expected.get_field("partial");
                if (!partial_datum) break;
                const R::Object* partial = partial_datum->get_object();
                if (!partial) break;
                for (const auto& it : *partial) {
                    if (!object->count(it.first) || !equal((*object).at(it.first), it.second)) {
                        return false;
                    }
                    return true;
                }
            }
            const R::Array* array = got.get_array();
            if (array) {
                const R::Datum* partial_datum = expected.get_field("partial");
                if (!partial_datum) break;
                const R::Array* partial = partial_datum->get_array();
                if (!partial) break;

                for (const auto& want : *partial) {
                    bool match = false;
                    for (const auto& have : *array) {
                        if (equal(have, want)) {
                            match = true;
                            break;
                        }
                    }
                    if (match == false) return false;
                }
                return true;
            }
        } else if(*type == "uuid") {
            const std::string* string = got.get_string();
            if (string && string->size() == 36) {
                return true;
            }
        } else if (*type == "regex") {
            const R::Datum* regex_datum = expected.get_field("regex");
            if (!regex_datum) break;
            const std::string* regex = regex_datum->get_string();
            if (!regex) break;
            const std::string* str = got.get_string();
            if (!str) break;
            return match(str->c_str(), regex->c_str());
        }
    } while(0);
    const R::Object* got_object = got.get_object();
    const R::Object* expected_object = expected.get_object();
    if (got_object && expected_object) {
        R::Object have = *got_object;
        for (const auto& it : *expected_object) {
            auto other = have.find(it.first);
            if (other == have.end()) return false;
            if (!equal(other->second, it.second)) return false;
            have.erase(other);
        }
        for (auto& it : have) {
            if (!falsey(std::move(it.second))) {
                return false;
            }
        }
        return true;
    }
    const R::Array* got_array = got.get_array();
    const R::Array* expected_array = expected.get_array();
    if (got_array && expected_array) {
        if (got_array->size() != expected_array->size()) return false;
        for (R::Array::const_iterator i = got_array->begin(), j = expected_array->begin();
             i < got_array->end();
             i++, j++) {
            if(!equal(*i, *j)) return false;
        }
        return true;
    }
    return got == expected;
}

R::Object partial(R::Array&& array) {
    return R::Object{{"special", "partial"}, {"partial", std::move(array)}};
}

R::Object regex(const char* pattern) {
    return R::Object{{"special", "regex"}, {"regex", pattern}};
}

void clean_slate() {
    R::table_list().for_each([](R::Var t){ return R::table_drop(*t); });
    R::db("rethinkdb").table("_debug_scratch").delete_().run(*conn);
}

const char* indent() {
    static const char spaces[] = "                                       ";
    return spaces + sizeof(spaces) - 1 - 2 * section.size();
}

std::string truncate(std::string&& string) {
    if (string.size() > 200) {
        return string.substr(0, 197) + "...";
    }
    return string;
}

int len(const R::Datum& d) {
    const R::Array* arr = d.get_array();
    if (!arr) throw ("testlib: len: expected an array but got " + to_string(d));
    return arr->size();
}

R::Query wait(int n) {
    sleep(n);
    return R::expr(n);
}

R::Datum nil = R::Nil();

R::Array append(R::Array lhs, R::Array rhs) {
    if (lhs.empty()) {
        lhs = std::move(rhs);
    } else {
        lhs.reserve(lhs.size() + rhs.size());
        std::move(std::begin(rhs), std::end(rhs), std::back_inserter(lhs));
        rhs.clear();
    }

    return lhs;
}

