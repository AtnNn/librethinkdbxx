#pragma once

#include "net.h"

namespace RethinkDB {

class Cursor {
public:
    Cursor(Token&&);
    Cursor(Token&&, Response&&);

    Datum& next();
    void each(std::function<void(Datum&&)>);
    Array toArray();
    void close();

private:
    void add_response(Response&&);
    void add_results(Array&&);

    bool no_more = false;
    size_t index = 0;
    Array buffer;
    Token token;
};

}
