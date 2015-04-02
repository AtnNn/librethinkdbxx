#pragma once

#include <string>
#include <vector>
#include <map>

namespace RethinkDB {

class Datum;

class Time;
class Point;
class Line;
class Polygon;
class Binary;

using Array = std::vector<Datum>;
using Object = std::map<std::string, const Datum>;

class Datum {
public:
    static Datum nil() { return Datum(); }
    Datum(bool boolean_) : type(Type::BOOLEAN), value(boolean_) { }
    Datum(double number_) : type(Type::NUMBER), value(number_) { }
    Datum(const std::string& string_) : type(Type::STRING), value(string_) { }
    Datum(std::string&& string_) : type(Type::STRING), value(std::move(string_)) { }
    Datum(const Object& object_) : type(Type::OBJECT), value(object_) { }
    Datum(Object&& object_) : type(Type::OBJECT), value(std::move(object_)) { }
    Datum(const Array& array_) : type(Type::ARRAY), value(array_) { }
    Datum(Array&& array_) : type(Type::ARRAY), value(std::move(array_)) { }

    Datum(const Datum& other) : type(other.type), value(other.type, other.value) { }

    ~Datum(){
        switch(type){
        case Type::NIL: break;
        case Type::BOOLEAN: break;
        case Type::NUMBER: break;
        case Type::STRING: { using namespace std; value.string.~string(); } break;
        case Type::OBJECT: value.object.~Object(); break;
        case Type::ARRAY: value.array.~Array(); break;
        }
    }

private:
    enum class Type {
        NIL, BOOLEAN, NUMBER, STRING, OBJECT, ARRAY,
        // TIME, POINT, LINE, POLYGON, BINARY
    };
    const Type type;

    union datum_value {
        const bool boolean;
        const double number;
        const std::string string;
        const Object object;
        const Array array;

        datum_value() { }
        datum_value(bool boolean_) : boolean(boolean_) { }
        datum_value(double number_) : number(number_) { }
        datum_value(const std::string& string_) : string(string_) { }
        datum_value(std::string&& string_) : string(std::move(string_)) { }
        datum_value(const Object& object_) : object(object_) { }
        datum_value(Object&& object_) : object(std::move(object_)) { }
        datum_value(const Array& array_) : array(array_) { }
        datum_value(Array&& array_) : array(std::move(array_)) { }

        datum_value(Type type, const datum_value& other){
            switch(type){
            case Type::NIL: break;
            case Type::BOOLEAN: new (this) bool(other.boolean); break;
            case Type::NUMBER: new (this) double(other.number); break;
            case Type::STRING: new (this) std::string(other.string); break;
            case Type::OBJECT: new (this) Object(other.object); break;
            case Type::ARRAY: new (this) Array(other.array); break;
            }
        }
        ~datum_value() { }
    } value;

    Datum() : type(Type::NIL), value() { }
};


Datum nil() {
    return Datum::nil();
}

Datum datum(bool boolean) {
    return Datum(boolean);
}

Datum datum(double number) {
    return Datum(number);
}

Datum datum(const std::string& string) {
    return Datum(string);
}

Datum datum(std::string&& string) {
    return Datum(string);
}

template <class T>
Datum datum(const std::map<std::string, T>& map) {
    Object object;
    for (auto it : map) {
        std::pair<std::string, Datum> pair(it.left, datum(it.right));
        object.insert(pair);
    }
    return datum(std::move(object));
}

template <class T>
Datum datum(std::map<std::string, T>&& map) {
    Object object;
    for (auto it : map) {
        std::pair<std::string, Datum> pair(it.left, datum(std::move(it.right)));
        object.insert(pair);
    }
    return datum(std::move(object));
}

template <class T>
Datum datum(const std::vector<T>& vec) {
    Array array;
    for (auto it : vec) {
        array.insert(datum(it));
    }
    return datum(std::move(array));
}

template <class T>
Datum datum(std::vector<T>&& vec) {
    Array array;
    for (auto it : vec) {
        array.insert(datum(std::move(it)));
    }
    return datum(std::move(array));
}

}
