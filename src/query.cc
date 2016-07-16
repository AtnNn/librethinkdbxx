#include <cstdlib>
#include <set>

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
Query::Query(const Datum& datum_) : datum(datum_.apply<Datum>(datum_to_query)) { }

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

Cursor Query::run(Connection& conn, OptArgs&& opts) {
    if (!free_vars.empty()) {
        throw Error("run: query has free variables");
    }
    OutputBuffer out;
    write_datum(Array{static_cast<double>(Protocol::Query::QueryType::START), datum, Query(std::move(opts)).datum}, out);
    Token token = conn.start_query(out.buffer);
    auto it = opts.find("noreply");
    if (it != opts.end()) {
        bool* no_reply = it->second.datum.get_boolean();
        if (no_reply && *no_reply) {
            return Cursor(std::move(token), Nil());
        }
    }
    return Cursor(std::move(token));
}

struct {
    Datum operator() (Object&& object, const std::map<int, int>& subst, bool) {
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
CO0_IMPL(random, RANDOM)

Query row(TT::IMPLICIT_VAR, {});
Query minval(TT::MINVAL, {});
Query maxval(TT::MAXVAL, {});

Query binary(const std::string& data) {
    return expr(Binary(data));
}

Query binary(std::string&& data) {
    return expr(Binary(data));
}

Query binary(const char* data) {
    return expr(Binary(data));
}

struct {
    bool operator() (const Object& object) {
        for (const auto& it : object) {
            if (it.second.apply<bool>(*this)) {
                return true;
            }
        }
        return false;
    }
    bool operator() (const Array& array) {
        int type = *array[0].get_number();
        if (type == static_cast<int>(TT::IMPLICIT_VAR)) {
            return true;
        }
        if (type == static_cast<int>(TT::FUNC)) {
            return false;
        }
        for (const auto& it : *array[1].get_array()) {
            if (it.apply<bool>(*this)) {
                return true;
            }
        }
        if (array.size() == 3) {
            return array[2].apply<bool>(*this);
        } else {
            return false;
        }
    }
    template <class T>
    bool operator() (T) {
        return false;
    }
} needs_func_wrap;

Query Query::func_wrap(Query&& query) {
    if (query.datum.apply<bool>(needs_func_wrap)) {
        return Query(TT::FUNC, {expr(Array{new_var_id(query.free_vars)}), std::move(query)});
    }
    return query;
}

Query Query::func_wrap(const Query& query) {
    if (query.datum.apply<bool>(needs_func_wrap)) {
        // TODO return Query(TT::FUNC, {expr(Array{new_var_id(query.free_vars)}), query.copy()});
        return Query(Nil());
    }
    return query;
}

Query Query::make_object(std::vector<Query>&& args) {
    if (args.size() % 2 != 0) {
        return Query(TT::OBJECT, std::move(args));
    }
    std::set<std::string> keys;
    for (auto it = args.begin(); it != args.end() && it + 1 != args.end(); it += 2) {
        std::string* key = it->datum.get_string();
        if (!key || keys.count(*key)) {
            return Query(TT::OBJECT, std::move(args));
        }
        keys.insert(*key);
    }
    Query ret{Nil()};
    Object object;
    for (auto it = args.begin(); it != args.end(); it += 2) {
        std::string* key = it->datum.get_string();
        object.emplace(std::move(*key), ret.alpha_rename(std::move(*(it + 1))));
    }
    ret.datum = std::move(object);
    return ret;
}

Query Query::make_binary(Query&& query) {
    std::string* string = query.datum.get_string();
    if (string) {
        return expr(Binary(std::move(*string)));
    }
    return Query(TT::BINARY, std::vector<Query>{query});
}

Query::Query(OptArgs&& optargs) : datum(Nil()) {
    Object oargs;
    for (auto& it : optargs) {
        oargs.emplace(it.first, alpha_rename(std::move(it.second)));
    }        
    datum = std::move(oargs);
}

OptArgs optargs() {
    return OptArgs{};
}

Query january(TT::JANUARY, {});
Query february(TT::FEBRUARY, {});
Query march(TT::MARCH, {});
Query april(TT::APRIL, {});
Query may(TT::MAY, {});
Query june(TT::JUNE, {});
Query july(TT::JULY, {});
Query august(TT::AUGUST, {});
Query september(TT::SEPTEMBER, {});
Query october(TT::OCTOBER, {});
Query november(TT::NOVEMBER, {});
Query december(TT::DECEMBER, {});
Query monday(TT::MONDAY, {});
Query tuesday(TT::TUESDAY, {});
Query wednesday(TT::WEDNESDAY, {});
Query thursday(TT::THURSDAY, {});
Query friday(TT::FRIDAY, {});
Query saturday(TT::SATURDAY, {});
Query sunday(TT::SUNDAY, {});

Query Query::copy() const {
    return *this;
}

Datum Query::get_datum() const {
    return datum;
}

}
