#pragma once

#include "net.h"

namespace RethinkDB {

class Cursor {
public:
    Cursor(Token&&);
    Cursor(Token&&, Response&&);
    Cursor(Cursor&&) = default;

    ~Cursor();

    Datum& next();
    void each(std::function<void(Datum&&)>);
    Array to_array();
    void close();
    bool has_next();
    bool is_single() const;

private:
    void add_response(Response&&);
    void add_results(Array&&);

    bool single = false;
    bool no_more = false;
    size_t index = 0;
    Array buffer;
    Token token;
};

}
