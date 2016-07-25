#ifndef CONNECTION_P_H
#define CONNECTION_P_H

#include <unordered_map>

#include "connection.h"
#include "term.h"
#include "json_p.h"
#include "utils.h"

#include "rapidjson-config.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/encodedstream.h"
#include "rapidjson/document.h"

#include <thread>
#include "asio/io_service.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/connect.hpp"
#include "asio/write.hpp"
#include "asio/read.hpp"
#include "asio/read_until.hpp"
#include "asio/streambuf.hpp"
using asio::ip::tcp;

namespace RethinkDB {

extern const int debug_net;

struct Query {
    Protocol::Query::QueryType type;
    uint64_t token;
    Datum term;
    OptArgs optArgs;

    std::string serialize() {
        Array query_arr{static_cast<double>(type)};
        if (term.is_valid()) query_arr.emplace_back(term);
        if (!optArgs.empty())
            query_arr.emplace_back(Term(std::move(optArgs)).datum);

        std::string query_str = write_datum(query_arr);
        if (debug_net > 0) {
            fprintf(stderr, "[%" PRIu64 "] >> %s\n", token, query_str.c_str());
        }

        char header[12];
        memcpy(header, &token, 8);
        uint32_t size = query_str.size();
        memcpy(header + 8, &size, 4);
        query_str.insert(0, header, 12);
        return query_str;
    }
};

// Used internally to convert a raw response type into an enum
Protocol::Response::ResponseType response_type(double t);
Protocol::Response::ErrorType runtime_error_type(double t);

// Contains a response from the server. Use the Cursor class to interact with these responses
class Response {
public:
    Response() {}
    Response(Datum&& datum) :
        type(response_type(std::move(datum).extract_field("t").extract_number())),
        error_type(datum.get_field("e") ?
                   runtime_error_type(std::move(datum).extract_field("e").extract_number()) :
                   Protocol::Response::ErrorType(0)),
        result(std::move(datum).extract_field("r").extract_array()) { }
    Error as_error();
    Protocol::Response::ResponseType type;
    Protocol::Response::ErrorType error_type;
    Array result;
};

class ResponseParser
{
public:
    ResponseParser() : state(reading_token_and_length), desired_length(0) {}
    void reset() { state = reading_token_and_length; desired_length = 0; }
    enum result_type { good, bad, indeterminate };

    template <typename InputIterator>
    std::tuple<result_type, InputIterator, uint64_t> parse(Response& response,
        InputIterator begin, InputIterator end)
    {
        while (begin != end) {
            result_type result = consume(response, *begin++);
            if (result == good || result == bad)
                return std::make_tuple(result, begin, token);
        }

        return std::make_tuple(indeterminate, begin, token);
    }

private:
    result_type consume(Response& response, char input) {
        buffer.push_back(input);

        if (state == reading_token_and_length) {
            if (buffer.size() == 12) {
                memcpy(&token, buffer.data(), 8);
                uint32_t length;
                memcpy(&length, buffer.data() + 8, 4);

                buffer.clear();
                state = reading_datum;
                desired_length = length;
            }
            return indeterminate;
        }

        if (state == reading_datum) {
            if (buffer.size() != desired_length) {
                return indeterminate;
            }

            rapidjson::Document document;
            rapidjson::MemoryStream ms(buffer.data(), buffer.size());
            rapidjson::EncodedInputStream<rapidjson::UTF8<>, rapidjson::MemoryStream> eis(ms);
            document.ParseStream(eis);
            response = Response(read_datum(document));

            buffer.clear();
            state = reading_token_and_length;
            return good;
        }

        return indeterminate;
    }

    enum parser_state {
        reading_token_and_length,
        reading_datum
    } state;

    std::vector<char> buffer;
    uint64_t token;
    uint32_t desired_length;
};

class ConnectionPrivate {
public:
    ConnectionPrivate(std::string host_, int port_, std::string auth_key_, tcp::socket &&socket_)
        : guarded_next_token(1),
          host(host_), port(port_), auth_key(auth_key_),
          socket(std::move(socket_)) {}
    Response wait_for_response(uint64_t, double);
    void run_query(Query query, bool no_reply = false);

    void start_read_loop();
    void read_loop();

    uint64_t new_token() {
        return guarded_next_token++;
    }

    std::mutex cache_lock;

    struct TokenCache {
        bool closed = false;
        std::condition_variable cond;
        std::queue<Response> responses;
    };

    std::map<uint64_t, TokenCache> guarded_cache;
    uint64_t guarded_next_token;
    int guarded_sockfd;
    bool guarded_loop_active;

    std::string host;
    int port;
    std::string auth_key;
    tcp::socket socket;

    // parsing
    std::array<char, 8192> buffer;
    ResponseParser parser;
    Response current_response;
};

class CacheLock {
public:
    CacheLock(ConnectionPrivate& conn) : inner_lock(conn.cache_lock) { }

    void lock() {
        inner_lock.lock();
    }

    void unlock() {
        inner_lock.unlock();
    }

    std::unique_lock<std::mutex> inner_lock;
};

}   // namespace RethinkDB

#endif  // CONNECTION_P_H
