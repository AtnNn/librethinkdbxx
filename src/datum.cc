#include "datum.h"

namespace RethinkDB {

Datum nil() {
    return Datum::nil();
}

Datum datum(bool boolean) {
    return Datum(boolean);
}

Datum datum(double number) {
    return Datum(number);
}

Datum datum(int number) {
    return Datum((double)number);
}

Datum datum(const std::string& string) {
    return Datum(string);
}

Datum datum(std::string&& string) {
    return Datum(string);
}

Datum datum(const Datum& datum) {
    return Datum(datum);
}

Datum datum(Datum&& datum) {
    return Datum(std::move(datum));
}

}
