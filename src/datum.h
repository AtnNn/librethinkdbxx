#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "protocol_defs.h"
#include "error.h"
#include "types.h"

namespace RethinkDB {

class Datum {
public:
    Datum(Nil) : type(Type::NIL), value() { }
    Datum(bool boolean_) : type(Type::BOOLEAN), value(boolean_) { }
    Datum(double number_) : type(Type::NUMBER), value(number_) { }
    Datum(const std::string& string_) : type(Type::STRING), value(string_) { }
    Datum(std::string&& string_) : type(Type::STRING), value(std::move(string_)) { }
    Datum(const Array& array_) : type(Type::ARRAY), value(array_) { }
    Datum(Array&& array_) : type(Type::ARRAY), value(std::move(array_)) { }
    Datum(const Binary& binary) : type(Type::BINARY), value(binary) { }
    Datum(Binary&& binary) : type(Type::BINARY), value(std::move(binary)) { }

    Datum(const Object& object_) : type(Type::OBJECT), value(object_) {
        convert_pseudo_type();
    }
    Datum(Object&& object_) : type(Type::OBJECT), value(std::move(object_)) {
        convert_pseudo_type();
    }

    Datum(const Datum& other) : type(other.type), value(other.type, other.value) { }
    Datum(Datum&& other) : type(other.type), value(other.type, std::move(other.value)) { }

    Datum& operator=(const Datum& other) {
        value.destroy(type);
        type = other.type;
        value.set(type, other.value);
        return *this;
    }

    Datum& operator=(Datum&& other) {
        value.destroy(type);
        type = other.type;
        value.set(type, std::move(other.value));
        return *this;
    }

    Datum(uint32_t number_) : Datum(static_cast<double>(number_)) { }
    Datum(uint64_t number_) : Datum(static_cast<double>(number_)) { }
    Datum(int32_t number_) : Datum(static_cast<double>(number_)) { }
    Datum(int64_t number_) : Datum(static_cast<double>(number_)) { }
    Datum(Protocol::Term::TermType type) : Datum(static_cast<double>(type)) { }
    Datum(const char* string) : Datum(static_cast<std::string>(string)) { }


    template <class T>
    Datum(const std::map<std::string, T>& map) : type(Type::OBJECT), value(Object()) {
        for (const auto& it : map) {
            value.object.emplace(it.left, Datum(it.right));
        }
    }

    template <class T>
    Datum(std::map<std::string, T>&& map) : type(Type::OBJECT), value(Object()) {
        for (auto& it : map) {
            value.object.emplace(it.first, Datum(std::move(it.second)));
        }
    }

    template <class T>
    Datum(const std::vector<T>& vec) : type(Type::ARRAY), value(Array()) {
        for (const auto& it : vec) {
            value.array.emplace_back(it);
        }
    }

    template <class T>
    Datum(std::vector<T>&& vec) : type(Type::ARRAY), value(Array()) {
        for (auto& it : vec) {
            value.array.emplace_back(std::move(it));
        }
    }

    ~Datum() {
        value.destroy(type);
    }

    template <class R, class F, class ...A>
    R apply(F f, A&& ...args) const & {
        switch (type) {
        case Type::NIL: return f(Nil(), std::forward<A>(args)...); break;
        case Type::BOOLEAN: return f(value.boolean, std::forward<A>(args)...); break;
        case Type::NUMBER: return f(value.number, std::forward<A>(args)...); break;
        case Type::STRING: return f(value.string, std::forward<A>(args)...); break;
        case Type::OBJECT: return f(value.object, std::forward<A>(args)...); break;
        case Type::ARRAY: return f(value.array, std::forward<A>(args)...); break;
        case Type::BINARY: return f(value.binary, std::forward<A>(args)...); break;
        }
        throw Error("Impossible");
    }

    template <class R, class F, class ...A>
    R apply(F f, A&& ...args) && {
        switch (type) {
        case Type::NIL: return f(Nil(), std::forward<A>(args)...); break;
        case Type::BOOLEAN: return f(std::move(value.boolean), std::forward<A>(args)...); break;
        case Type::NUMBER: return f(std::move(value.number), std::forward<A>(args)...); break;
        case Type::STRING: return f(std::move(value.string), std::forward<A>(args)...); break;
        case Type::OBJECT: return f(std::move(value.object), std::forward<A>(args)...); break;
        case Type::ARRAY: return f(std::move(value.array), std::forward<A>(args)...); break;
        case Type::BINARY: return f(std::move(value.binary), std::forward<A>(args)...); break;
        }
        throw Error("Impossible");
    }

    bool is_nil() const;

    bool* get_boolean();
    const bool* get_boolean() const;
    double* get_number();
    const double* get_number() const;
    std::string* get_string();
    const std::string* get_string() const;
    Object* get_object();
    const Object* get_object() const;
    Datum* get_field(std::string);
    const Datum* get_field(std::string) const;
    Array* get_array();
    const Array* get_array() const;
    Datum* get_nth(size_t);
    const Datum* get_nth(size_t) const;
    Binary* get_binary();
    const Binary* get_binary() const;

    bool& extract_boolean();
    double& extract_number();
    std::string& extract_string();
    Object& extract_object();
    Datum& extract_field(std::string);
    Array& extract_array();
    Datum& extract_nth(size_t);
    Binary& extract_binary();

    int compare(const Datum&) const;
    bool operator== (const Datum&) const;

    Datum to_raw();

private:
    void convert_pseudo_type();

    enum class Type {
        ARRAY, BOOLEAN, NIL, NUMBER, OBJECT, BINARY, STRING
        // TIME, POINT, LINE, POLYGON
    };
    Type type;

    union datum_value {
        bool boolean;
        double number;
        std::string string;
        Object object;
        Array array;
        Binary binary;

        datum_value() { }
        datum_value(bool boolean_) : boolean(boolean_) { }
        datum_value(double number_) : number(number_) { }
        datum_value(const std::string& string_) : string(string_) { }
        datum_value(std::string&& string_) : string(std::move(string_)) { }
        datum_value(const Object& object_) : object(object_) { }
        datum_value(Object&& object_) : object(std::move(object_)) { }
        datum_value(const Array& array_) : array(array_) { }
        datum_value(Array&& array_) : array(std::move(array_)) { }
        datum_value(const Binary& binary_) : binary(binary_) { }
        datum_value(Binary&& binary_) : binary(std::move(binary_)) { }

        datum_value(Type type, const datum_value& other){
            set(type, other);
        }

        datum_value(Type type, datum_value&& other){
            set(type, std::move(other));
        }

        void set(Type type, datum_value&& other) {
            switch(type){
            case Type::NIL: break;
            case Type::BOOLEAN: new (this) bool(other.boolean); break;
            case Type::NUMBER: new (this) double(other.number); break;
            case Type::STRING: new (this) std::string(std::move(other.string)); break;
            case Type::OBJECT: new (this) Object(std::move(other.object)); break;
            case Type::ARRAY: new (this) Array(std::move(other.array)); break;
            case Type::BINARY: new (this) Binary(std::move(other.binary)); break;
            }
        }

        void set(Type type, const datum_value& other) {
            switch(type){
            case Type::NIL: break;
            case Type::BOOLEAN: new (this) bool(other.boolean); break;
            case Type::NUMBER: new (this) double(other.number); break;
            case Type::STRING: new (this) std::string(other.string); break;
            case Type::OBJECT: new (this) Object(other.object); break;
            case Type::ARRAY: new (this) Array(other.array); break;
            case Type::BINARY: new (this) Binary(other.binary); break;
            }
        }

        void destroy(Type type) {
            switch(type){
            case Type::NIL: break;
            case Type::BOOLEAN: break;
            case Type::NUMBER: break;
            case Type::STRING: { typedef std::string str; string.~str(); } break;
            case Type::OBJECT: object.~Object(); break;
            case Type::ARRAY: array.~Array(); break;
            case Type::BINARY: binary.~Binary(); break;
            } 
        }

        ~datum_value() { }
    };

    datum_value value;
};

}
