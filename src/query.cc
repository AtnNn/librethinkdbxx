#include <cstdlib>

#include "query.h"
#include "stream.h"
#include "json.h"

namespace RethinkDB {

using TT = Protocol::Term::TermType;

struct {
    Datum operator() (const Array& array) {
        Array copy;
        copy.reserve(array.size());
        for (const auto& it : array) {
            copy.emplace_back(it.apply<Datum>(*this));
        }
        return Datum(Array{TT::MAKE_ARRAY, std::move(copy)});
    }
    Datum operator() (const Object& object) {
        Object copy;
        for (const auto& it : object) {
            copy.emplace(it.first, it.second.apply<Datum>(*this));
        }
        return std::move(copy);
    }
    template<class T>
    Datum operator() (T&& atomic) {
        return Datum(std::forward<T>(atomic));
    }
} datum_to_query;

Query::Query(Datum&& datum_) : datum(datum_.apply<Datum>(datum_to_query)) { }

Datum Query::run(Connection& conn) {
    if (!free_vars.empty()) {
        throw Error("run: query still has free variables");
    }
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
            return Datum(cursor.to_array());
        }
    case RT::WAIT_COMPLETE:
    case RT::CLIENT_ERROR:
    case RT::COMPILE_ERROR:
    case RT::RUNTIME_ERROR:
        throw response.as_error();
    }
    throw Error("Impossible");
}

Query::Query(Query&& orig, OptArgs&& new_optargs) : datum(Nil()) {
    Datum* cur = orig.datum.get_nth(2);
    Object optargs;
    free_vars = std::move(orig.free_vars);
    if (cur) {
        optargs = std::move(cur->extract_object());
    }
    for (auto& it : new_optargs) {
        optargs.emplace(std::move(it.first), alpha_rename(std::move(it.second)));
    }
    datum = Array{ std::move(orig.datum.extract_nth(0)), std::move(orig.datum.extract_nth(1)), std::move(optargs) };
}

Query nil() {
    return Query(Nil());
}

Cursor Query::run_cursor(Connection& conn) {
    if (!free_vars.empty()) {
        throw Error("run: query still has free variables");
    }
    OutputBuffer out;
    write_datum(Array{static_cast<double>(Protocol::Query::QueryType::START), datum, Object{}}, out);
    Token token = conn.start_query(out.buffer);
    return Cursor(std::move(token));
}

struct {
    Datum operator() (Object&& object, const std::map<int, int>& subst, bool args) {
        Object ret;
        for (auto& it : object) {
            ret.emplace(std::move(it.first), std::move(it.second).apply<Datum>(*this, subst, false));
        }
        return ret;
    }
    Datum operator() (Array&& array, const std::map<int, int>& subst, bool args) {
        if (!args) {
            double cmd = array[0].extract_number();
            if (cmd == static_cast<int>(TT::VAR)) {
                double var = array[1].extract_nth(0).extract_number();
                auto it = subst.find(static_cast<int>(var));
                if (it != subst.end()) {
                    return Array{ TT::VAR, { it->second }};
                }
            }
            if (array.size() == 2) {
                return Array{ std::move(array[0]), std::move(array[1]).apply<Datum>(*this, subst, true) };
            } else {
                return Array{
                    std::move(array[0]),
                    std::move(array[1]).apply<Datum>(*this, subst, true),
                    std::move(array[2]).apply<Datum>(*this, subst, false) };
            }
        } else {
            Array ret;
            for (auto& it : array) {
                ret.emplace_back(std::move(it).apply<Datum>(*this, subst, false));
            }
            return ret;
        }
    }
    template <class T>
    Datum operator() (T&& a, const std::map<int, int>&, bool) {
        return std::move(a);
    }
} alpha_renamer;

static int new_var_id(const std::map<int, int*>& vars) {
    while (true) {
        int id = gen_var_id();
        if (vars.find(id) == vars.end()) {
            return id;
        }
    }
}

Datum Query::alpha_rename(Query&& query) {
    if (free_vars.empty()) {
        free_vars = std::move(query.free_vars);
        return std::move(query.datum);
    }
    
    std::map<int, int> subst;
    for (auto it = query.free_vars.begin(); it != query.free_vars.end(); ++it) {
        auto var = free_vars.find(it->first);
        if (var == free_vars.end()) {
            free_vars.emplace(it->first, it->second);
        } else if (var->second != it->second) {
            int id = new_var_id(free_vars);
            subst.emplace(it->first, id);
            free_vars.emplace(id, it->second);
        }
    }
    if (subst.empty()) {
        return std::move(query.datum);
    } else {
        return query.datum.apply<Datum>(alpha_renamer, subst, false);
    }
}

int gen_var_id() {
    return ::random() % (1<<30);
}


C0_IMPL(db_list, DB_LIST)
C0_IMPL(table_list, TABLE_LIST)
C0_IMPL(random, RANDOM)
C0_IMPL(now, NOW)
C0_IMPL(range, RANGE)
C0_IMPL(error, ERROR)
C0_IMPL(uuid, UUID)
C0_IMPL(literal, LITERAL)
CO0_IMPL(wait, WAIT)
C0_IMPL(rebalance, REBALANCE)

Query row(TT::IMPLICIT_VAR, {});
Query minval(TT::MAXVAL, {});
Query maxval(TT::MINVAL, {});

Query binary(const std::string& data) {
    return expr(Binary(data));
}

}
