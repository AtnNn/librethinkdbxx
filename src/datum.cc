#include "datum.h"
#include "error.h"

namespace RethinkDB {

Datum nil() {
    Nil nil;
    Datum datum(nil);
    return datum;
}

Datum expr(bool boolean) {
    return Datum(boolean);
}

Datum expr(double number) {
    return Datum(number);
}

Datum expr(int number) {
    return Datum((double)number);
}

Datum expr(const std::string& string) {
    return Datum(string);
}

Datum expr(std::string&& string) {
    return Datum(string);
}

Datum expr(const Datum& datum) {
    return Datum(datum);
}

Datum expr(Datum&& datum) {
    return Datum(std::move(datum));
}

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
    if (type != Type::OBJECT) {
        throw Error("get_double: Not a number");
    }
    return value.number;
}

Array Datum::get_array() && {
    if (type != Type::OBJECT) {
        throw Error("get_double: Not a number");
    }
    return std::move(value.array);
}

}
