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

namespace RethinkDB {

void skip_spaces(BufferedInputStream& stream) {
    while (true) {
        int c = stream.peek();
        if (c == EOF) {
            break;
        }
        if (!strchr(" \n\r\t", c)) {
            break;
        }
        stream.pos++;
    }
}

std::string read_string(BufferedInputStream& stream) {
    std::string string;
    const size_t buffer_size = 2048;
    char buffer[buffer_size];
    while (true) {
        size_t pos = 0;
        for (; pos < buffer_size - (max_utf8_encoded_size - 1); pos++) {
            int c = stream.next();
            size_t size;
            unsigned int code;
            switch (c) {
            case EOF:
                throw Error("EOF in JSON string");
            case '\\':
                c = stream.next();
                switch (c) {
                case EOF:
                    throw Error("EOF in JSON string");
                case '"':
                case '\\':
                    buffer[pos] = c;
                    break;
                case 'b':
                    buffer[pos] = '\b';
                    break;
                case 'f':
                    buffer[pos] = '\f';
                    break;
                case 't':
                    buffer[pos] = '\t';
                    break;
                case 'r':
                    buffer[pos] = '\r';
                    break;
                case 'n':
                    buffer[pos] = '\n';
                    break;
                case 'u':
                    code = 0;
                    for (int i = 0; i < 4; i ++) {
                        c = stream.next();
                        if (c >= '0' && c <= '9') {
                            code = code * 16 + c - '0';
                        } else if (c >= 'A' && c <= 'F') {
                            code = code * 16 + c - 'A' + 10;
                        } else if (c >= 'a' && c <= 'f') {
                            code = code * 16 + c - 'a' + 10;
                        } else {
                            throw Error("Invalid JSON string unicode escape character '%c'", c);
                        }
                    }
                    size = utf8_encode(code, buffer + pos);
                    pos += size - 1;
                    break;
                default:
                    throw Error("Invalid JSON string escape code '\\%c'", c);
                }
                break;
            case '"':
                string.append(buffer, pos);
                return string;
            default:
                buffer[pos] = c;
            }
        }
        string.append(buffer, pos);
        pos = 0;
    }
}

bool read_field(BufferedInputStream& stream, Object* object) {
    int c = stream.next();
    std::string key;
    switch (c) {
    case '"':
        key = read_string(stream);
        skip_spaces(stream);
        c = stream.next();
        if (c == ':') {
            auto val = std::pair<std::string, const Datum>(key, read_datum(stream));
            object->insert(std::move(val));
            skip_spaces(stream);
            c = stream.next();
            if (c == ',') {
                skip_spaces(stream);
                return true;
            } else if (c == '}') {
                return false;
            }
        }
    }
    if (c == EOF) {
        throw Error("EOF in JSON Object");
    } else {
        throw Error("Invalid character in JSON Object: 0x%x '%c'", c, c);
    }
}

Datum read_object(BufferedInputStream& stream) {
    Object object;
    skip_spaces(stream);
    if (stream.peek() == '}') {
        stream.pos ++;
    } else {
        while (read_field(stream, &object));
    }
    if (object.count("$reql_type$")) {
        return Datum(std::move(object)).from_raw();
    } else {
        return Datum(std::move(object));
    }
}

Datum read_array(BufferedInputStream& stream) {
    Array array;
    int c = stream.peek();
    if (c == ']') {
        stream.pos++;
        return Datum(std::move(array));
    }
    while (true) {
        array.emplace_back(read_datum(stream));
        int c = stream.peek();
        if (c == ',') {
            stream.pos++;
            continue;
        }
        if (c == ']') {
            stream.pos++;
            return Datum(std::move(array));
        } 
    } 
}

void read_exact(BufferedInputStream& stream, const char* str) {
    for (; *str; str++) {
        if (stream.next() != *str) {
            throw Error("Invalid JSON constant");
        }
    }
}

locale_t locale_c() {
    static locale_t ret = nullptr;
    if (!ret) {
        // TODO: this was only tested with glibc
        ret = newlocale(LC_ALL_MASK, "C", nullptr);
    }
    return ret;
}

Datum read_number(BufferedInputStream& stream, char first) {
    const size_t max_buf_size = 128;
    char buf[max_buf_size];
    buf[0] = first;
    size_t i = 1;
    while (true) {
        int c = stream.peek();
        if (c == EOF) break;
        if (strchr("0123456789.eE+-", c)) {
            buf[i++] = c;
            stream.pos++;
        } else {
            break;
        }
        if (i + 1 == max_buf_size) {
            throw Error("Unable to parse JSON number: too long");
        }
    }
    buf[i] = 0;
    char *end;
    double number = strtod_l(buf, &end, locale_c());
    if (*end != 0) {
        throw Error("Invalid JSON number '%s'", buf);
    }
    return Datum(number);
}

Datum read_datum(BufferedInputStream& stream) {
    skip_spaces(stream);
    int c = stream.next();
    switch (c) {
    case '{':
        return read_object(stream);
    case '[':
        return read_array(stream);
    case '"':
        return Datum(read_string(stream));
    case 't':
        read_exact(stream, "rue");
        return Datum(true);
    case 'f':
        read_exact(stream, "alse");
        return Datum(false);
    case 'n':
        read_exact(stream, "ull");
        return Datum(Nil());
        break;
    default:
        if (strchr("0123456789-", c)) {
            return read_number(stream, c);
        }
    }
    if (c == EOF) {
        throw Error("EOF in JSON");
    } else {
        throw Error("Invalid character in JSON: 0x%x '%c'", c, c);
    }
}

Datum read_datum(const std::string& string) {
    BufferedInputStream buffer(string);
    Datum datum = read_datum(buffer);
    skip_spaces(buffer);
    if (!buffer.eof()) {
        throw Error("Trailing character in JSON: '%c'", buffer.peek());
    }
    return datum;
}

struct datum_writer {
    void operator() (Nil, OutputStream& out) {
        out.write("null");
    }
    void operator() (bool boolean, OutputStream& out) {
        out.write(boolean ? "true" : "false");
    }
    void operator() (double number, OutputStream& out) {
        if (number == 0 && std::signbit(number) == 1) {
            out.write("-0.0");
        } else {
            out.printf("%.17lg", number);
        }
    }
    void operator() (const std::string& string, OutputStream& out) {
        out.write("\"");
        for (char c : string) {
            switch (c) {
            case '\n': out.write("\\n"); break;
            case '\r': out.write("\\r"); break;
            case '"': out.write("\\\""); break;
            case '\\': out.write("\\\\"); break;
            case 0: out.write("\\u0000"); break;
            default: out.write(&c, 1); break;
            }
        }
        out.write("\"");
    }
    void operator() (const Object& object, OutputStream& out) {
        out.write("{");
        bool first = true;
        for (const auto& it : object) {
            if (!first) {
                out.write(",");
            }
            (*this)(it.first, out);
            out.write(":");
            it.second.apply<void>(*this, out);
            first = false;
        }
        out.write("}");
    }
    void operator() (const Array& array, OutputStream& out) {
        out.write("[");
        bool first = true;
        for (const auto& it : array) {
            if (!first) {
                out.write(",");
            }
            it.apply<void>(*this, out);
            first = false; 
        }        
        out.write("]");
    }
    template <class T>
    void operator() (const T& a, OutputStream& out) {
        Datum(a).to_raw().apply<void>(*this, out);
    }
};

void write_datum(const Datum& datum, OutputStream& out) {
    datum_writer write_datum;
    datum.apply<void>(write_datum, out);
}

std::string write_datum(const Datum& datum) {
    OutputBuffer out;
    write_datum(datum, out);
    return out.buffer;
}

}
