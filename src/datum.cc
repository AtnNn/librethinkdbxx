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

bool Datum::extract_boolean() {
    if (type != Type::BOOLEAN) {
        throw Error("extract_bool: Not a boolean");
    }
    return value.boolean;
}

double Datum::extract_number() {
    if (type != Type::NUMBER) {
        throw Error("extract_number: Not a number: %s", write_datum(*this).c_str());
    }
    return value.number;
}

std::string Datum::extract_string() && {
    if (type != Type::STRING) {
        throw Error("extract_string: Not a string");
    }
    return std::move(value.string);
}

Object Datum::extract_object() && {
    if (type != Type::OBJECT) {
        throw Error("extract_object: Not an object");
    }
    return std::move(value.object);
}

Datum Datum::extract_field(std::string key) && {
    if (type != Type::OBJECT) {
        throw Error("extract_field: Not an object");
    }
    auto it = value.object.find(key);
    if (it == value.object.end()) {
        throw Error("extract_field: No such key in object");
    }
    return std::move(it->second);
}

Array Datum::extract_array() && {
    if (type != Type::ARRAY) {
        throw Error("get_array: Not an array");
    }
    return std::move(value.array);
}

}
