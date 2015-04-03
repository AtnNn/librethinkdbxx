#pragma once

#include "datum.h"
#include "protocol_defs.h"

namespace RethinkDB {

class Query {
    Query(const Datum& datum);
    Query(Datum&&);

    Query(Protocol::Term::TermType type, Array&& args) :
        datum(Array{ static_cast<int>(type), std::move(args)}) { }

    Query(Protocol::Term::TermType type, Array&& args, Object&& optargs) :
        datum(Array{ static_cast<int>(type), std::move(args), std::move(optargs)}) { }

private:
    Datum datum;
};

}
