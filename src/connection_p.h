#ifndef CONNECTION_P_H
#define CONNECTION_P_H

#include "connection.h"
#include "term.h"
#include "json_p.h"

#include "rapidjson-config.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/encodedstream.h"
#include "rapidjson/document.h"

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

class Token;
class ConnectionPrivate {
public:
    ConnectionPrivate() : guarded_next_token(1) {}
    Response wait_for_response(uint64_t, double);
    void run_query(Query query, bool no_reply = false);

    uint64_t new_token() {
        return guarded_next_token++;
    }

    std::mutex read_lock;
    std::mutex write_lock;
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

class ReadLock {
public:
    ReadLock(ConnectionPrivate& conn) : lock(conn.read_lock), conn(&conn) { }

    size_t recv_some(char*, size_t, double wait);
    void recv(char*, size_t, double wait);
    std::string recv(size_t);
    size_t recv_cstring(char*, size_t);

    Response read_loop(uint64_t, CacheLock&&, double);

    std::lock_guard<std::mutex> lock;
    ConnectionPrivate* conn;
};

class WriteLock {
public:
    WriteLock(ConnectionPrivate& conn) : lock(conn.write_lock), conn(&conn) { }

    void send(const char*, size_t);
    void send(std::string);

    std::lock_guard<std::mutex> lock;
    ConnectionPrivate* conn;
};

class SocketReadStream {
public:
    typedef char Ch;    //!< Character type (byte).

    //! Constructor.
    /*!
        \param fp File pointer opened for read.
        \param buffer user-supplied buffer.
        \param bufferSize size of buffer in bytes. Must >=4 bytes.
    */
    SocketReadStream(ReadLock* reader, char* buffer, size_t bufferSize)
        : reader_(reader),
          buffer_(buffer),
          bufferSize_(bufferSize),
          bufferLast_(0),
          current_(buffer_),
          readCount_(0),
          count_(0),
          eof_(false)
    {
        RAPIDJSON_ASSERT(reader_ != 0);
        RAPIDJSON_ASSERT(bufferSize >= 4);
        Read();
    }

    Ch Peek() const { return *current_; }
    Ch Take() { Ch c = *current_; Read(); return c; }
    size_t Tell() const { return count_ + static_cast<size_t>(current_ - buffer_); }

    // Not implemented
    void Put(Ch) { RAPIDJSON_ASSERT(false); }
    void Flush() { RAPIDJSON_ASSERT(false); }
    Ch* PutBegin() { RAPIDJSON_ASSERT(false); return 0; }
    size_t PutEnd(Ch*) { RAPIDJSON_ASSERT(false); return 0; }

    // For encoding detection only.
    const Ch* Peek4() const {
        return (current_ + 4 <= bufferLast_) ? current_ : 0;
    }

private:
    void Read() {
        if (current_ < bufferLast_) {
            ++current_;
        } else if (!eof_) {
            count_ += readCount_;
            readCount_ = reader_->recv_some(buffer_, bufferSize_, FOREVER);
            bufferLast_ = buffer_ + readCount_ - 1;
            current_ = buffer_;

            if ((count_ + readCount_) == bufferSize_) {
                buffer_[readCount_] = '\0';
                ++bufferLast_;
                eof_ = true;
            }
        }
    }

    ReadLock* reader_;
    Ch *buffer_;
    size_t bufferSize_;
    Ch *bufferLast_;
    Ch *current_;
    size_t readCount_;
    size_t count_;  //!< Number of characters read
    bool eof_;
};

}   // namespace RethinkDB

#endif  // CONNECTION_P_H
