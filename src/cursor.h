#pragma once

#include "net.h"

namespace RethinkDB {

class Cursor;

class Cursor {
public:
    Cursor(Token&&);
    Cursor(Token&&, Response&&);
    Cursor(Cursor&&) = default;
    Cursor(Token&&, Datum&&);

    ~Cursor();

    class iterator {
    public:
        iterator(Cursor*);
        iterator& operator++ ();
        Datum& operator* ();
        bool operator!= (const iterator&) const;

    private:
        Cursor *cursor;
    };

    Datum& next() const;
    Datum& peek() const;
    void each(std::function<void(Datum&&)>) const;
    Array&& to_array() &&;
    Datum to_datum() &&;
    Array to_array() const &;
    Datum to_datum() const &;
    void close() const;
    bool has_next() const;
    bool is_single() const;
    iterator begin();
    iterator end();

private:
    void add_response(Response&&) const;
    void add_results(Array&&) const;
    void clear_and_read_all() const;
    void convert_single() const;

    mutable bool single = false;
    mutable bool no_more = false;
    mutable size_t index = 0;
    mutable Array buffer;
    Token token;
};

}
