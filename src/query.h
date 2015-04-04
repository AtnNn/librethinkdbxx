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

    using OptArgs = std::map<std::string, Query>;

    Query(Protocol::Term::TermType type, std::vector<Query>&& args, OptArgs&& optargs) : datum(Array()) {
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

#define C0(name, type) Query name() && {                                \
        return Query(TT::type, std::vector<Query>{ std::move(*this) }); }
#define C1(name, type) template <class T> Query name(T&& a) && {        \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)) }); }
#define C2(name, type) template <class T, class U> Query name(T&& a, U&& b) && { \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)) }); }
#define C_(name, type) template <class ...T> Query name(T&& ...a) && {  \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a))... }); }
#define CO0(name, type) Query name(OptArgs&& optarg) && {                \
        return Query(TT::type, std::vector<Query>{ std::move(*this) }, std::move(optarg)); }
#define CO1(name, type) template <class T> Query name(T&& a, OptArgs&& optarg = {}) && { \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)) }, std::move(optarg)); }
#define CO2(name, type) template <class T, class U> Query name(T&& a, U&& b, OptArgs&& optarg = {}) && { \
        return Query(TT::type, std::vector<Query>{ std::move(*this),    \
                    expr(std::forward<T>(a)), expr(std::forward<U>(b)) }, std::move(optarg)); }

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
    C0(changes, CHANGES)
    CO1(insert, INSERT)
    CO1(update, UPDATE)
    CO1(replace, REPLACE)
    CO0(delete_, DELETE)
    C0(sync, SYNC)
    CO1(table, TABLE)
    C1(get, GET)
    C_(get_all, GET_ALL)
    CO2(between, BETWEEN)
    CO1(filter, FILTER)
    C2(inner_join, INNER_JOIN)
    C2(outer_join, OUTER_JOIN)
    CO2(eq_join, EQ_JOIN)
    C0(zip, ZIP)
    C_(map, MAP)
    C_(with_fields, WITH_FIELDS)
    C1(concat_map, CONCAT_MAP)
    C_(order_by, ORDER_BY)
    C1(skip, SKIP)
    C1(limit, LIMIT)
    CO1(slice, SLICE)
    CO2(slice, SLICE)
    C1(nth, NTH)
    C1(offsets_of, OFFSETS_OF)
    C0(is_empty, IS_EMPTY)
    C1(union_, UNION)
    C1(sample, SAMPLE)
    C_(group, GROUP)
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
    C0(literal, LITERAL)
    C1(literal, LITERAL)
    C_(object, OBJECT)
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
    C1(config, CONFIG)
    C1(rebalance, REBALANCE)
    CO0(reconfigure, RECONFIGURE)
    C0(status, STATUS)
    CO0(wait, WAIT) 

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
        for (auto it = list.begin(); it + 1 != args.end(); ++it) {
            args.emplace_back(std::move(*it));
        }
        return Query(TT::FUNCALL, args);
    }    

    Query opt(OptArgs&& optargs) && {
        return Query(std::move(datum), std::move(optargs));
    }

    // TODO: binary

private:
    Query(Datum&& orig, OptArgs&& optargs);
    Datum datum;
};

Query nil();

template <class T>
Query expr(T&& a) {
    return Query(std::forward<T>(a));
}

#define C0(name) Query name();
#define C0_IMPL(name, type) Query name() { return Query(TT::type, std::vector<Query>{}); }
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
#define CO1(name, type) template <class T> Query name(T&& a, Object&& optarg = {}) { \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a)), optarg }); }
#define CO2(name, type) template <class T, class U> Query name(T&& a, U&& b, Object&& optarg = {}) { \
        return Query(TT::type, std::vector<Query>{ expr(std::forward<T>(a)), std::forward<U>(b)}, optarg); }

C1(db_create, DB_CREATE)
C1(db_drop, DB_DROP)
C0(db_list)
CO1(table_create, TABLE_CREATE)
C1(table_drop, TABLE_DROP)
C0(table_list)
C1(db, DB)
CO1(table, TABLE)
C0(row)
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

}
