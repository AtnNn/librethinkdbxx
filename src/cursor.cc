#include "cursor.h"

namespace RethinkDB {

Cursor::Cursor(Token&& token_) : token(std::move(token_)) {
    token.ask_for_more();
}

Cursor::Cursor(Token&& token_, Response&& response) : token(std::move(token_)) {
    if (response.type == Protocol::Response::ResponseType::SUCCESS_PARTIAL) {
        token.ask_for_more();
    }
    add_response(std::move(response));
}

Cursor::~Cursor() {
    close();
}

Datum& Cursor::next() {
    while (true) {
        if (index == buffer.size()) {
            if (no_more) {
                throw Error("next: No more data");
            }
            add_response(token.wait_for_response());
        } else {
            return buffer[index++];
        }
    }
}

void Cursor::each(std::function<void(Datum&&)> f) {
    while (true) {
        while (index == buffer.size()) {
            if (no_more) {
                return;
            } else {
                add_response(token.wait_for_response());
            }
        }
        f(std::move(buffer[index++]));
    }
}

Array Cursor::to_array() {
    if (index != 0) {
        buffer.erase(buffer.begin(), buffer.begin() + index);
        index = 0;
    }
    while (!no_more) {
        add_response(token.wait_for_response());
    }
    index = buffer.size();
    return buffer;
}

void Cursor::close() {
    token.close();
    no_more = true;
}

bool Cursor::has_next() {
    while (true) {
        if (index == buffer.size()) {
            if (no_more) {
                return false;
            }
            add_response(token.wait_for_response());
        } else {
            return true;
        }
    }
}

bool Cursor::is_single() {
    return single;
}

void Cursor::add_results(Array&& results) {
    if (index == buffer.size()) {
        buffer = std::move(results);
        index = 0;
    } else {
        buffer.reserve(buffer.size() + results.size());
        for (auto& it : results) {
            buffer.emplace_back(std::move(it));
        }
    }
}

void Cursor::add_response(Response&& response) {
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
        break;
    case RT::WAIT_COMPLETE:
    case RT::CLIENT_ERROR:
    case RT::COMPILE_ERROR:
    case RT::RUNTIME_ERROR:
        throw response.as_error(); 
    }
}

}
