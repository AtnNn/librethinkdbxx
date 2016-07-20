#include "cursor.h"
#include "exceptions.h"

namespace RethinkDB {

Cursor::Cursor(Token&& token_) : token(std::move(token_)) {
    add_response(token.wait_for_response(FOREVER));
    if (!no_more) {
        // token.ask_for_more();
    }
}

Cursor::Cursor(Token&& token_, Response&& response) : token(std::move(token_)) {
    add_response(std::move(response));
    if (!no_more) {
        // token.ask_for_more();
    }
}

Cursor::Cursor(Token&& token_, Datum&& datum) :
    single(true), no_more(true), buffer(Array{std::move(datum)}), token(std::move(token_)) { }

Cursor::Cursor(Datum&& d) {
    buffer.emplace_back(d);
    index = 0;
    single = true;
    no_more = true;
}

Cursor::~Cursor() {
    if (!no_more) {
        close();
    }
}

Datum& Cursor::next(double wait) const {
    if (!has_next(wait)) {
        throw Error("next: No more data");
    }
    return buffer[index++];
}

Datum& Cursor::peek(double wait) const {
    if (!has_next(wait)) {
        throw Error("next: No more data");
    }
    return buffer[index];
}

void Cursor::each(std::function<void(Datum&&)> f, double wait) const {
    while (has_next(wait)) {
        f(std::move(buffer[index++]));
    }
}

void Cursor::convert_single() const {
    if (index != 0) {
        throw Error("Cursor: already consumed");
    }
    if (buffer.size() != 1) {
        throw Error("Cursor: invalid response from server");
    }
    Array* array = buffer[0].get_array();
    if (!array) {
        throw Error("Cursor: not an array");
    }
    buffer = std::move(*array);
    single = false;
}

void Cursor::clear_and_read_all() const {
    if (single) {
        convert_single();
    }
    if (index != 0) {
        buffer.erase(buffer.begin(), buffer.begin() + index);
        index = 0;
    }
    while (!no_more) {
        add_response(token.wait_for_response(FOREVER));
    }
}

Array&& Cursor::to_array() && {
    clear_and_read_all();
    return std::move(buffer);
}

Array Cursor::to_array() const & {
    clear_and_read_all();
    return buffer;
}

Datum Cursor::to_datum() const & {
    if (single) {
        if (index != 0) {
            throw Error("to_datum: already consumed");
        }
        return buffer[0];
    } else {
        clear_and_read_all();
        return buffer;
    }
}

Datum Cursor::to_datum() && {
    Datum ret((Nil()));
    if (single) {
        if (index != 0) {
            throw Error("to_datum: already consumed");
        }
        ret = std::move(buffer[0]);
    } else {
        clear_and_read_all();
        ret = std::move(buffer);
    }
    return ret;
}

void Cursor::close() const {
    token.close();
    no_more = true;
}

bool Cursor::has_next(double wait) const {
    if (single) {
        convert_single();
    }
    while (true) {
        if (index >= buffer.size()) {
            if (no_more) {
                return false;
            }
            add_response(token.wait_for_response(wait));
        } else {
            return true;
        }
    }
}

bool Cursor::is_single() const {
    return single;
}

void Cursor::add_results(Array&& results) const {
    if (index >= buffer.size()) {
        buffer = std::move(results);
        index = 0;
    } else {
        buffer.reserve(buffer.size() + results.size());
        for (auto& it : results) {
            buffer.emplace_back(std::move(it));
        }
    }
}

void Cursor::add_response(Response&& response) const {
    using RT = Protocol::Response::ResponseType;
    switch (response.type) {
    case RT::SUCCESS_SEQUENCE:
        add_results(std::move(response.result));
        no_more = true;
        break;
    case RT::SUCCESS_PARTIAL:
        token.ask_for_more();
        add_results(std::move(response.result));
        break;
    case RT::SUCCESS_ATOM:
        add_results(std::move(response.result));
        single = true;
        no_more = true;
        break;
    case RT::SERVER_INFO:
        add_results(std::move(response.result));
        single = true;
        no_more = true;
        break;
    case RT::WAIT_COMPLETE:
    case RT::CLIENT_ERROR:
    case RT::COMPILE_ERROR:
    case RT::RUNTIME_ERROR:
        no_more = true;
        throw response.as_error();
    }
}

Cursor::iterator Cursor::begin() {
    return iterator(this);
}

Cursor::iterator Cursor::end() {
    return iterator(NULL);
}

Cursor::iterator::iterator(Cursor* cursor_) : cursor(cursor_) { }

Cursor::iterator& Cursor::iterator::operator++ () {
    if (cursor == NULL) {
        throw Error("incrementing an exhausted Cursor iterator");
    }
    cursor->next();
    return *this;
}

Datum& Cursor::iterator::operator* () {
    if (cursor == NULL) {
        throw Error("reading from empty Cursor iterator");
    }
    return cursor->peek();
}

bool Cursor::iterator::operator!= (const Cursor::iterator& other) const {
    if (cursor == other.cursor) {
        return false;
    }
    return !((cursor == NULL && !other.cursor->has_next()) ||
             (other.cursor == NULL && !cursor->has_next()));
}

}
