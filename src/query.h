#pragma once

#include "datum.h"
#include "net.h"
#include "protocol_defs.h"
#include "cursor.h"

namespace RethinkDB {

using TT = Protocol::Term::TermType;

class Query;
class Var;

template <class T>
Query expr(T&&);

int gen_var_id();

using OptArgs = std::map<std::string, Query>;

class Query {
public:
    Query(const Query& other) : free_vars(other.free_vars), datum(other.datum) { }
    Query(Query&& other) : free_vars(std::move(other.free_vars)), datum(std::move(other.datum)) { }

    explicit Query(Datum&& a);

    Query(std::function<Query()> f) : datum(Nil()) { set_function<std::function<Query()>>(f); }
    Query(std::function<Query(Var)> f) : datum(Nil()) { set_function<std::function<Query(Var)>, Var>(f); }
    Query(std::function<Query(Var, Var)> f) : datum(Nil()) { set_function<std::function<Query(Var, Var)>, Var, Var>(f); }

    Query(Protocol::Term::TermType type, std::vector<Query>&& args) : datum(Array()) {
        Array dargs;
        for (auto& it : args) {
            dargs.emplace_back(alpha_rename(std::move(it)));
        }
        datum = Datum(Array{ type, std::move(dargs) });
    }

    Query(Protocol::Term::TermType type, std::vector<Query>&& args, OptArgs&& optargs) : datum(Array()) {
        Array dargs;
        for (auto& it : args) {
            dargs.emplace_back(alpha_rename(std::move(it)));
        }
        Object oargs;
        for (auto& it : optargs) {
            oargs.emplace(it.first, alpha_rename(std::move(it.second)));
        }        
        datum = Array{ type, std::move(dargs), std::move(oargs) };
    }

    Query copy() const {
        return *this;
    }

#define C0(name, type) \
    Query name() &&      { return Query(TT::type, std::vector<Query>{ std::move(*this) }); } \
    Query name() const & { return Query(TT::type, std::vector<Query>{ this->copy()     }); }
#define C1(name, type) \
    template <class T> \
    Query name(T&& a) && { return Query(TT::type, std::vector<Query>{ std::move(*this), expr(std::forward<T>(a)) }); } \
    template <class T> \
    Query name(T&& a) const & { return Query(TT::type, std::vector<Query>{ this->copy(), expr(std::forward<T>(a)) }); }
#define C2(name, type)                                                  \
    template <class T, class U> Query name(T&& a, U&& b) && {           \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)) }); } \
    template <class T, class U> Query name(T&& a, U&& b) const & {      \
        return Query(TT::type, std::vector<Query>{ this->copy(),        \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)) }); }
#define C_(name, type)                                                  \
    template <class ...T> Query name(T&& ...a) && {                     \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a))... }); }                   \
    template <class ...T> Query name(T&& ...a) const & {                \
        return Query(TT::type, std::vector<Query>{ this->copy(),        \
                    expr(std::forward<T>(a))... }); }
#define CO0(name, type)                                                 \
    Query name(OptArgs&& optarg = {}) && {                              \
        return Query(TT::type, std::vector<Query>{ std::move(*this) }, std::move(optarg)); } \
    Query name(OptArgs&& optarg = {}) const & {                         \
        return Query(TT::type, std::vector<Query>{ this->copy() }, std::move(optarg)); }
#define CO1(name, type)                                                 \
    template <class T> Query name(T&& a, OptArgs&& optarg = {}) && {    \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)) }, std::move(optarg)); }   \
    template <class T> Query name(T&& a, OptArgs&& optarg = {}) const & { \
        return Query(TT::type, std::vector<Query>{ this->copy(),        \
                    expr(std::forward<T>(a)) }, std::move(optarg)); }
#define CO2(name, type)                                                 \
    template <class T, class U> Query name(T&& a, U&& b, OptArgs&& optarg = {}) && { \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)) }, std::move(optarg)); } \
    template <class T, class U> Query name(T&& a, U&& b, OptArgs&& optarg = {}) const & { \
        return Query(TT::type, std::vector<Query>{ this->copy(),        \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)) }, std::move(optarg)); }
#define CO3(name, type)                                                 \
    template <class T, class U, class V> Query name(T&& a, U&& b, V&& c, OptArgs&& optarg = {}) && { \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)), \
                    expr(std::forward<V>(c)) }, std::move(optarg)); } \
    template <class T, class U, class V> Query name(T&& a, U&& b, V&& c, OptArgs&& optarg = {}) const & { \
        return Query(TT::type, std::vector<Query>{ this->copy(),        \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)), \
                    expr(std::forward<V>(c))}, std::move(optarg)); }
#define CO4(name, type)                                                 \
    template <class T, class U, class V, class W> Query name(T&& a, U&& b, V&& c, W&& d, OptArgs&& optarg = {}) && { \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)), \
                    expr(std::forward<V>(c)), expr(std::forward<W>(d)) }, std::move(optarg)); } \
    template <class T, class U, class V, class W> Query name(T&& a, U&& b, V&& c, W&& d, OptArgs&& optarg = {}) const & { \
        return Query(TT::type, std::vector<Query>{ this->copy(),        \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)), \
                    expr(std::forward<V>(c)), expr(std::forward<W>(d)) }, std::move(optarg)); }
#define CO_(name, type) \
    C_(name, type)      \
    CO0(name, type)     \
    CO1(name, type)     \
    CO2(name, type)     \
    CO3(name, type)     \
    CO4(name, type)


    CO1(table_create, TABLE_CREATE)
    C1(table_drop, TABLE_DROP)
    C0(table_list, TABLE_LIST)
    CO1(index_create, INDEX_CREATE)
    CO2(index_create, INDEX_CREATE)
    C1(index_drop, INDEX_DROP)
    C0(index_list, INDEX_LIST)
    CO2(index_rename, INDEX_RENAME)
    C_(index_status, INDEX_STATUS)
    C_(index_wait, INDEX_WAIT)
    CO0(changes, CHANGES)
    CO1(insert, INSERT)
    CO1(update, UPDATE)
    CO1(replace, REPLACE)
    CO0(delete_, DELETE)
    C0(sync, SYNC)
    CO1(table, TABLE)
    C1(get, GET)
    CO_(get_all, GET_ALL)
    CO2(between, BETWEEN)
    CO1(filter, FILTER)
    C2(inner_join, INNER_JOIN)
    C2(outer_join, OUTER_JOIN)
    CO2(eq_join, EQ_JOIN)
    C0(zip, ZIP)
    C_(map, MAP)
    C_(with_fields, WITH_FIELDS)
    C1(concat_map, CONCAT_MAP)
    CO_(order_by, ORDER_BY)
    C1(skip, SKIP)
    C1(limit, LIMIT)
    CO1(slice, SLICE)
    CO2(slice, SLICE)
    C1(nth, NTH)
    C1(offsets_of, OFFSETS_OF)
    C0(is_empty, IS_EMPTY)
    C1(union_, UNION)
    C1(sample, SAMPLE)
    CO_(group, GROUP)
    C0(ungroup, UNGROUP)
    C1(reduce, REDUCE)
    C0(count, COUNT)
    C1(count, COUNT)
    C0(sum, SUM)
    C1(sum, SUM)
    C0(avg, AVG)
    C1(avg, AVG)
    C1(min, MIN)
    CO0(min, MIN)
    C1(max, MAX)
    CO0(max, MAX)
    CO0(distinct, DISTINCT)
    C_(contains, CONTAINS)
    C_(pluck, PLUCK)
    C_(without, WITHOUT)
    C1(merge, MERGE)
    C1(append, APPEND)
    C1(prepend, PREPEND)
    C1(difference, DIFFERENCE)
    C1(set_insert, SET_INSERT)
    C1(set_union, SET_UNION)
    C1(set_intersection, SET_INTERSECTION)
    C1(set_difference, SET_DIFFERENCE)
    C1(operator[], BRACKET)
    C1(get_field, GET_FIELD)
    C_(has_fields, HAS_FIELDS)
    C2(insert_at, INSERT_AT)
    C2(splice_at, SPLICE_AT)
    C1(delete_at, DELETE_AT)
    C2(delete_at, DELETE_AT)
    C2(change_at, CHANGE_AT)
    C0(keys, KEYS)
    C1(match, MATCH)
    C0(split, SPLIT)
    C1(split, SPLIT)
    C2(split, SPLIT)
    C0(upcase, UPCASE)
    C0(downcase, DOWNCASE)
    C_(add, ADD)
    C1(operator+, ADD)
    C1(sub, SUB)
    C1(operator-, SUB)
    C_(mul, MUL)
    C1(operator*, MUL)
    C1(div, DIV)
    C1(operator/, DIV)
    C1(mod, MOD)
    C1(operator%, MOD)
    C_(and_, AND)
    C1(operator&&, AND)
    C_(or_, OR)
    C1(operator||, OR)
    C1(eq, EQ)
    C1(operator==, EQ)
    C1(ne, NE)
    C1(operator!=, NE)
    C1(gt, GT)
    C1(operator>, GT)
    C1(ge, GE)
    C1(operator>=, GE)
    C1(lt, LT)
    C1(operator<, LT)
    C1(le, LE)
    C1(operator<=, LE)
    C0(not_, NOT)
    C0(operator!, NOT)
    C1(in_timezone, IN_TIMEZONE)
    C0(timezone, TIMEZONE)
    CO2(during, DURING)
    C0(date, DATE)
    C0(time_of_day, TIME_OF_DAY)
    C0(year, YEAR)
    C0(month, MONTH)
    C0(day, DAY)
    C0(day_of_week, DAY_OF_WEEK)
    C0(day_of_year, DAY_OF_YEAR)
    C0(hours, HOURS)
    C0(minutes, MINUTES)
    C0(seconds, SECONDS)
    C0(to_iso8601, TO_ISO8601)
    C0(to_epoch_time, TO_EPOCH_TIME)
    C1(for_each, FOR_EACH)
    C1(default_, DEFAULT)
    CO1(js, JAVASCRIPT)
    C1(coerce_to, COERCE_TO)
    C0(type_of, TYPE_OF)
    C0(info, INFO)
    C1(to_json, TO_JSON_STRING)
    C1(distance, DISTANCE)
    C0(fill, FILL)
    C0(to_geojson, TO_GEOJSON)
    CO1(get_intersecting, GET_INTERSECTING)
    CO1(get_nearest, GET_NEAREST)
    C1(includes, INCLUDES)
    C1(intersects, INTERSECTS)
    C1(polygon_sub, POLYGON_SUB)
    C0(config, CONFIG)
    C0(rebalance, REBALANCE)
    CO0(reconfigure, RECONFIGURE)
    C0(status, STATUS)
    CO0(wait, WAIT) 
    C_(operator(), FUNCALL)

#undef C0
#undef C1
#undef C2
#undef C_
#undef CO0
#undef CO1
#undef CO2

    Datum run(Connection&);
    Cursor run_cursor(Connection&);

    template <class ...T>
    Query do_(T&& ...a) && {
        auto list = { expr(std::forward<T>(a))... };
        std::vector<Query> args;
        args.reserve(list.size() + 1);
        args.emplace_back(std::move(*(list.end()-1)));
        args.emplace_back(std::move(*this));
        for (auto it = list.begin(); it + 1 != list.end(); ++it) {
            args.emplace_back(std::move(*it));
        }
        return Query(TT::FUNCALL, std::move(args));
    }    

    Query opt(OptArgs&& optargs) && {
        return Query(std::move(*this), std::move(optargs));
    }

    // TODO: binary

private:
    friend class Var;

    template <class _>
    Var mkvar(std::vector<int>& vars);

    template <class F, class ...A>
    void set_function(F);

    Datum alpha_rename(Query&&);

    Query(Query&& orig, OptArgs&& optargs);

    std::map<int, int*> free_vars;
    Datum datum;
};

Query nil();

template <class T>
Query expr(T&& a) {
    return Query(std::forward<T>(a));
}

class Var {
public:
    Var(int* id_) : id(id_) { }
    Query operator*() const {
        Query query(TT::VAR, std::vector<Query>{expr(*id)});
        query.free_vars = {{*id, id}};
        return query;
    }
private:
    int* id;
};

template <class _>
Var Query::mkvar(std::vector<int>& vars) {
    int id = gen_var_id();
    vars.push_back(id);
    return Var(&*vars.rbegin());
}

template <class F, class ...A>
void Query::set_function(F f) {
    std::vector<int> vars;
    vars.reserve(sizeof...(A));
    Query body = f(mkvar<A>(vars)...);
    int* low = &*vars.begin();
    int* high = &*vars.end();
    for (auto it = body.free_vars.begin(); it != body.free_vars.end(); ) {
        if (it->second >= low && it->second < high) {
            if (it->first != *it->second) {
                throw Error("Internal error: variable index mis-match");
            }
            ++it;
        } else {
            free_vars.emplace(*it);
            ++it;
        }
    }
    datum = Array{TT::FUNC, Array{Array{TT::MAKE_ARRAY, vars}, body.datum}};
}


#define C0(name) Query name();
#define C0_IMPL(name, type) Query name() { return Query(TT::type, std::vector<Query>{}); }
#define CO0(name) Query name(OptArgs&& optargs = {});
#define CO0_IMPL(name, type) Query name(OptArgs&& optargs) { return Query(TT::type, std::vector<Query>{}, std::move(optargs)); }
#define C1(name, type) template <class T> Query name(T&& a) { \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a)) }); }
#define C2(name, type) template <class T, class U> Query name(T&& a, U&& b) { \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a)), expr(std::forward<U>(b)) }); }
#define C3(name, type) template <class A, class B, class C>    \
    Query name(A&& a, B&& b, C&& c) { return Query(TT::type, std::vector<Query>{ \
                expr(std::forward<A>(a)), expr(std::forward<B>(b)), expr(std::forward<C>(c)) }); }
#define C4(name, type) template <class A, class B, class C, class D>    \
    Query name(A&& a, B&& b, C&& c, D&& d) { return Query(TT::type, std::vector<Query>{ \
                expr(std::forward<A>(a)), expr(std::forward<B>(b)),     \
                    expr(std::forward<C>(c)), expr(std::forward<D>(d))}); }
#define C7(name, type) template <class A, class B, class C, class D, class E, class F, class G> \
    Query name(A&& a, B&& b, C&& c, D&& d, E&& e, F&& f, G&& g) { return Query(TT::type, std::vector<Query>{ \
        expr(std::forward<A>(a)), expr(std::forward<B>(b)), expr(std::forward<C>(c)), \
        expr(std::forward<D>(d)), expr(std::forward<E>(e)), expr(std::forward<F>(f)), \
            expr(std::forward<G>(g))}); }
#define C_(name, type) template <class ...T> Query name(T&& ...a) { \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a))... }); }
#define CO1(name, type) template <class T> Query name(T&& a, OptArgs&& optarg = {}) { \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a))}, std::move(optarg)); }
#define CO2(name, type) template <class T, class U> Query name(T&& a, U&& b, OptArgs&& optarg = {}) { \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a)), std::forward<U>(b)}, std::move(optarg)); }
#define CB(name, type)                                                  \
    template <class T> Query name(T&& a, Query&& b) {                   \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a)), std::move(b) }); } \
    template <class T> Query name(T&& a, const Query& b) {                   \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a)), b.copy() }); }

C1(db_create, DB_CREATE)
C1(db_drop, DB_DROP)
C0(db_list)
CO1(table_create, TABLE_CREATE)
C1(table_drop, TABLE_DROP)
C0(table_list)
C1(db, DB)
CO1(table, TABLE)
C_(add, ADD)
C2(sub, SUB)
C_(mul, MUL)
C2(div, DIV)
C2(mod, MOD)
C_(and_, AND)
C_(or_, OR)
C2(eq, EQ)
C2(ne, NE)
C2(gt, GT)
C2(ge, GE)
C2(lt, LT)
C2(le, LE)
C1(not_, NOT)
C0(random)
C0(now)
C4(time, TIME)
C7(time, TIME)
C1(epoch_time, EPOCH_TIME)
CO1(iso8601, ISO8601)
CO1(js, JAVASCRIPT)
C1(args, ARGS)
C3(branch, BRANCH)
C0(range)
C1(range, RANGE)
C2(range, RANGE)
C0(error)
C1(error, ERROR)
C1(json, JSON)
CO1(http, HTTP)
C0(uuid)
CO2(circle, CIRCLE)
C1(geojson, GEOJSON)
C_(line, LINE)
C2(point, POINT)
C_(polygon, POLYGON)
C_(object, OBJECT)
C_(array, MAKE_ARRAY)
C1(desc, DESC)
C1(asc, ASC)
C0(literal)
C1(literal, LITERAL)
CO0(wait)
C0(rebalance)

#undef C0
#undef C1
#undef C2
#undef C3
#undef C4
#undef C7
#undef C_
#undef CO1
#undef CO2

template <class R, class ...T>
Query do_(R&& a, T&& ...b) {
    return expr(std::forward<R>(a)).do_(std::forward<T>(b)...);
}

extern Query row;
extern Query maxval;
extern Query minval;

Query binary(const std::string& data);

}
