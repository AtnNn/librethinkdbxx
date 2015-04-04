#include "datum.h"
#include "error.h"
#include "json.h"

namespace RethinkDB {

bool Datum::is_nil() {
    return type == Type::NIL;
}

bool* Datum::get_boolean() {
    if (type == Type::BOOLEAN) {
        return &value.boolean;
    } else {
        return NULL;
    }
}

double* Datum::get_number() {
    if (type == Type::NUMBER) {
        return &value.number;
    } else {
        return NULL;
    }
}

std::string* Datum::get_string() {
    if (type == Type::STRING) {
        return &value.string;
    } else {
        return NULL;
    }
}

Datum* Datum::get_field(std::string key) {
    if (type != Type::OBJECT) {
        return NULL;
    }
    auto it = value.object.find(key);
    if (it == value.object.end()) {
        return NULL;
    }
    return &it->second;
}

Object* Datum::get_object() {
    if (type == Type::OBJECT) {
        return &value.object;
    } else {
        return NULL;
    }
}

Array* Datum::get_array() {
    if (type == Type::ARRAY) {
        return &value.array;
    } else {
        return NULL;
    }
}

bool& Datum::extract_boolean() {
    if (type != Type::BOOLEAN) {
        throw Error("extract_bool: Not a boolean");
    }
    return value.boolean;
}

double& Datum::extract_number() {
    if (type != Type::NUMBER) {
        throw Error("extract_number: Not a number: %s", write_datum(*this).c_str());
    }
    return value.number;
}

std::string& Datum::extract_string() {
    if (type != Type::STRING) {
        throw Error("extract_string: Not a string");
    }
    return value.string;
}

Object& Datum::extract_object() {
    if (type != Type::OBJECT) {
        throw Error("extract_object: Not an object");
    }
    return value.object;
}

Datum& Datum::extract_field(std::string key) {
    if (type != Type::OBJECT) {
        throw Error("extract_field: Not an object");
    }
    auto it = value.object.find(key);
    if (it == value.object.end()) {
        throw Error("extract_field: No such key in object");
    }
    return it->second;
}

Array& Datum::extract_array() {
    if (type != Type::ARRAY) {
        throw Error("get_array: Not an array");
    }
    return value.array;
}

int Datum::compare(const Datum& other) const {
#define COMPARE(a, b) do {          \
    if (a < b) { return -1; }       \
    if (a > b) { return 1; } } while(0)
#define COMPARE_OTHER(x) COMPARE(x, other.x)

    COMPARE_OTHER(type);
    int c;
    switch (type) {
    case Type::NIL: break;
    case Type::BOOLEAN: COMPARE_OTHER(value.boolean); break;
    case Type::NUMBER: COMPARE_OTHER(value.number); break;
    case Type::STRING:
        c = value.string.compare(other.value.string);
        COMPARE(c, 0);
        break;
    case Type::ARRAY:
        COMPARE_OTHER(value.array.size());
        for (size_t i = 0; i < value.array.size(); i++) {
            c = value.array[i].compare(other.value.array[i]);
            COMPARE(c, 0);
        }
        break;
    case Type::OBJECT:
        COMPARE_OTHER(value.array.size());
        for (Object::const_iterator l = value.object.begin(),
                 r = other.value.object.begin();
             l != value.object.end();
             ++l, ++r) {
            COMPARE(l->first, r->first);
            c = l->second.compare(r->second);
            COMPARE(c, 0);
        }
        break;
    }
    return 0;
#undef COMPARE_OTHER
#undef COMPARE
}

bool Datum::operator== (const Datum& other) {
    return compare(other) == 0;
}

}
