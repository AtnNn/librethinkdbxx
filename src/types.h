#pragma once

#include <vector>
#include <map>

namespace RethinkDB {

class Datum;

struct Nil { };

using Array = std::vector<Datum>;
using Object = std::map<std::string, Datum>;

struct Binary {
    bool operator== (const Binary& other) const {
        return data == other.data;
    }

    Binary(const std::string& data_) : data(data_) { }
    Binary(std::string&& data_) : data(std::move(data_)) { }
    std::string data;
};


class Time;
class Point;
class Line;
class Polygon;

}
