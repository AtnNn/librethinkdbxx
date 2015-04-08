#pragma once

#include <vector>
#include <map>
#include <ctime>

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


struct Time {
    Time(double epoch_time_, double utc_offset_ = 0) :
        epoch_time(epoch_time_), utc_offset(utc_offset_) { }

    static Time now() {
        return Time(time(NULL));
    }

    static bool parse_utc_offset(const std::string&, double*);
    static double parse_utc_offset(const std::string&);
    static std::string utc_offset_string(double);

    double epoch_time;
    double utc_offset;
};

class Point;
class Line;
class Polygon;

}
