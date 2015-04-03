#pragma once

#include "datum.h"
#include "net.h"
#include "protocol_defs.h"

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
        for (auto it : args) {
            dargs.emplace_back(std::move(it.datum));
        }
        datum = Datum(Array{ type, std::move(dargs) });
    }

    Query(Protocol::Term::TermType type, std::vector<Query>&& args, std::map<std::string, Query>&& optargs) : datum(Array()) {
        Array dargs;
        for (auto it : args) {
            dargs.emplace_back(std::move(it.datum));
        }
        Object oargs;
        for (auto it : optargs) {
            oargs.emplace(it.first, std::move(it.second.datum));
        }        
        datum = Array{ type, std::move(dargs), std::move(oargs) };
    }

    template <class T>
    Query operator+ (T&& a) && {
        return Query(TT::ADD, std::vector<Query>{ std::move(*this), expr(std::forward<T>(a)) });
    }

    Query count() && {
        return Query(TT::COUNT, std::vector<Query>{ std::move(*this) });
    }

    Datum run(Connection&);

private:
    Datum datum;
};

Query nil();

template <class T>
Query expr(T&& a) {
    return Query(std::forward<T>(a));
}

template <class T>
Query table(T&& table) {
    return Query(TT::TABLE, std::vector<Query>{ expr(std::forward<T>(table)) });
}

}
