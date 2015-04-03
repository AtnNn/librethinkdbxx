#include "query.h"

namespace RethinkDB {

class {
    Datum operator() (const Array& array) {
        Array copy;
        copy.reserve(array.size());
        for (auto it : array) {
            copy.emplace_back(it.apply<Datum>(datum_to_query));
        }
        return datum(Array{TT::MAKE_ARRAY, std::move(copy)});
    }
    template<class T>
    Datum operator() (T&& atomic) {
        return Datum(atomic);
    }
} datum_to_query;

Query::Query(const Datum& datum) :
    datum(datum.apply<Datum>(datum_to_query)) { }

Query::Query(Datum&& datum) :
    datum(Datum("TODO")) { }

}
