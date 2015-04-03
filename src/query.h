#pragma once

#include "datum.h"
#include "net.h"
#include "protocol_defs.h"

namespace RethinkDB {

class Query {
public:
    Query(const Datum& datum);

    Query(Protocol::Term::TermType type, Array&& args) :
        datum(Array{ type, std::move(args)}) { }

    Query(Protocol::Term::TermType type, Array&& args, Object&& optargs) :
        datum(Array{ type, std::move(args), std::move(optargs)}) { }

    template <class T>
    Query operator+ (T&& a) && {
        return Query(TT::ADD, Array{ std::move(*this), std::forward<T>(a) });
    }

    Datum run(Connection&);

private:
    Datum datum;
};

}
