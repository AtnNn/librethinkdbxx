#include "datum.h"
#include "error.h"
#include "json.h"

namespace RethinkDB {

Datum Datum::get_field(std::string key) && {
    if (type != Type::OBJECT) {
        throw Error("get_field: Not an object");
    }
    auto it = value.object.find(key);
    if (it == value.object.end()) {
        throw Error("get_field: No such key in object");
    }
    return std::move(it->second);
}

double Datum::get_double() {
    if (type != Type::NUMBER) {
        throw Error("get_double: Not a number: %s", write_datum(*this).c_str());
    }
    return value.number;
}

Array Datum::get_array() && {
    if (type != Type::ARRAY) {
        throw Error("get_array: Not an array");
    }
    return std::move(value.array);
}

}
