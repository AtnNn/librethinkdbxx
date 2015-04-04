#include "query.h"
#include "stream.h"
#include "json.h"

namespace RethinkDB {

Datum datum_to_query_t::operator() (const Array& array) {
    Array copy;
    copy.reserve(array.size());
    for (const auto& it : array) {
        copy.emplace_back(it.apply<Datum>(*this));
    }
    return Datum(Array{TT::MAKE_ARRAY, std::move(copy)});
}

Datum datum_to_query_t::operator() (const Object& object) {
    Object copy;
    for (const auto& it : object) {
        copy.emplace(it.first, it.second.apply<Datum>(*this));
    }
    return std::move(copy);
}

datum_to_query_t datum_to_query;

Datum Query::run(Connection& conn) {
    OutputBuffer out;
    write_datum(Array{static_cast<double>(Protocol::Query::QueryType::START), datum, Object{}}, out);
    Token token = conn.start_query(out.buffer);
    Response response = token.wait_for_response();
    using RT = Protocol::Response::ResponseType;
    Array array;
    switch (response.type) {
    case RT::SUCCESS_ATOM:
        if (response.result.size() != 1) {
            throw Error("Invalid response");
        }
        return Datum(std::move(response.result[0]));
    case RT::SUCCESS_SEQUENCE:
        return Datum(std::move(response.result));
    case RT::SUCCESS_PARTIAL:
        {
            Cursor cursor(std::move(token), std::move(response));
            return Datum(cursor.toArray());
        }
    case RT::WAIT_COMPLETE:
    case RT::CLIENT_ERROR:
    case RT::COMPILE_ERROR:
    case RT::RUNTIME_ERROR:
        throw response.as_error();
    }
    throw Error("Impossible");
}

Query nil() {
    return Query(Nil());
}

Cursor Query::run_cursor(Connection& conn) {
    OutputBuffer out;
    write_datum(Array{static_cast<double>(Protocol::Query::QueryType::START), datum, Object{}}, out);
    Token token = conn.start_query(out.buffer);
    return Cursor(std::move(token));
}

COMMAND0_IMPL(range, RANGE);

}
