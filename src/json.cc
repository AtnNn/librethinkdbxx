#include "config.h"

#if defined(USE_LOCALE_H)
#include <locale.h>
#elif defined(USE_XLOCALE_H)
#include <stdlib.h>
#include <xlocale.h>
#endif

#include <cmath>
#include <cstring>

#include "json.h"
#include "error.h"
#include "utils.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"

namespace RethinkDB {

Datum read_datum(const std::string& json) {
    rapidjson::Document document;
    document.Parse(json);
    return read_datum(document);
}

Datum read_datum(const rapidjson::Value &json) {
    switch(json.GetType()) {
    case rapidjson::kNullType: {
        return Nil();
    } break;
    case rapidjson::kFalseType: {
        return Datum(false);
    } break;
    case rapidjson::kTrueType: {
        return Datum(true);
    } break;
    case rapidjson::kObjectType: {
        Object result;
        for (rapidjson::Value::ConstMemberIterator it = json.MemberBegin();
             it != json.MemberEnd(); ++it) {
            result.emplace(std::make_pair(std::string(it->name.GetString(),
                                          it->name.GetStringLength()),
                                          read_datum(it->value)));
        }

        if (result.count("$reql_type$"))
            return Datum(std::move(result)).from_raw();
        return Datum(std::move(result));
    } break;
    case rapidjson::kArrayType: {
        Array result;
        for (rapidjson::Value::ConstValueIterator it = json.Begin();
             it != json.End(); ++it) {
            result.emplace_back(read_datum(*it));
        }
        return Datum(std::move(result));
    } break;
    case rapidjson::kStringType: {
        return Datum(std::string(json.GetString(), json.GetStringLength()));
    } break;
    case rapidjson::kNumberType: {
        return Datum(json.GetDouble());
    } break;
    }
}

std::string write_datum(const Datum& datum) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    datum.write_json(&writer);
    return std::string(buffer.GetString(), buffer.GetSize());
}

}
