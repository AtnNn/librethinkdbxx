#pragma once

#include "datum.h"
#include "net.h"
#include "protocol_defs.h"
#include "cursor.h"

namespace RethinkDB {

class Query;

template <class T>
Query expr(T&&);

struct datum_to_query_t {
    Datum operator() (const Array& array);
    Datum operator() (const Object& object);
    template<class T>
    Datum operator() (T&& atomic) {
        return Datum(std::forward<T>(atomic));
    }
};

extern datum_to_query_t datum_to_query;

class Query {
public:
    Query(const Query& other) : datum(other.datum) { }
    Query(Query&& other) : datum(std::move(other.datum)) { }

    template <class T>
    explicit Query(T&& a) : datum(Datum(a).apply<Datum>(datum_to_query)) { }

    Query(Protocol::Term::TermType type, std::vector<Query>&& args) : datum(Array()) {
        Array dargs;
        for (auto& it : args) {
            dargs.emplace_back(std::move(it.datum));
        }
        datum = Datum(Array{ type, std::move(dargs) });
    }

    Query(Protocol::Term::TermType type, std::vector<Query>&& args, std::map<std::string, Query>&& optargs) : datum(Array()) {
        Array dargs;
        for (auto& it : args) {
            dargs.emplace_back(std::move(it.datum));
        }
        Object oargs;
        for (auto& it : optargs) {
            oargs.emplace(it.first, std::move(it.second.datum));
        }        
        datum = Array{ type, std::move(dargs), std::move(oargs) };
    }

#define COMMAND0(name, type) Query name() && {         \
        return Query(TT::type, std::vector<Query>{ std::move(*this) }); }
#define COMMAND1(name, type) template <class T> Query name(T&& a) && { \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)) }); }
#define COMMAND2(name, type) template <class T, class U> Query name(T&& a, U&& b) && { \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)) }); }

    COMMAND1(operator+, ADD);
    COMMAND0(count, COUNT);

#undef COMMAND0
#undef COMMAND1
#undef COMMAND2

    Datum run(Connection&);
    Cursor run_cursor(Connection&);

private:
    Datum datum;
};

Query nil();

template <class T>
Query expr(T&& a) {
    return Query(std::forward<T>(a));
}

#define COMMAND0(name) Query name();
#define COMMAND0_IMPL(name, type) Query name() { return Query(TT::type, std::vector<Query>{}); }
#define COMMAND1(name, type) template <class T> Query name(T&& a) { \
        return Query(TT::type, std::vector<Query>{  expr(std::forward<T>(a)) }); }
#define COMMAND2(name, type) template <class T, class U> Query name(T&& a, U&& b) { \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a)), expr(std::forward<U>(b)) }); }

COMMAND1(table, TABLE);
COMMAND0(range);
COMMAND1(range, RANGE);
COMMAND2(range, RANGE);

#undef COMMAND0
#undef COMMAND1
#undef COMMAND2

}
