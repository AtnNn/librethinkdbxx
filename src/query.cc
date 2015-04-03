#include "query.h"
#include "stream.h"
#include "json.h"

namespace RethinkDB {

struct {
    Datum operator() (const Array& array) {
        Array copy;
        copy.reserve(array.size());
        for (auto it : array) {
            copy.emplace_back(it.apply<Datum>(*this));
        }
        return expr(Array{TT::MAKE_ARRAY, std::move(copy)});
    }
    Datum operator() (const Object& object) {
        Object copy;
        for (auto it : object) {
            copy.emplace(it.first, it.second.apply<Datum>(*this));
        }
        return std::move(copy);
    }
    template<class T>
    Datum operator() (T&& atomic) {
        return Datum(std::forward<T>(atomic));
    }
} datum_to_query;

Query::Query(const Datum& datum) :
    datum(datum.apply<Datum>(datum_to_query)) { }

Datum Query::run(Connection& conn) {
    OutputBuffer out;
    write_datum(datum, out);
    Token token = conn.start_query(out.buffer);
    while (true) {
        token.wait_for_response();
        
    }
}

}
