
#include <cstring>

#include "json.h"
#include "error.h"

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

const size_t max_utf8_encoded_size = 6;

size_t utf8_encode(unsigned int code, char* buf) {
    if (!(code & ~0x7F)) {
        buf[0] = code;
        return 1;
    } else if (!(code & ~0x7FF)) {
        buf[0] = 0xC0 | (code >> 6);
        buf[1] = 0x80 | (code & 0x3F);
        return 2;
    } else if (!(code & ~0xFFFF)) {
        buf[0] = 0xE0 | (code >> 12);
        buf[1] = 0x80 | ((code >> 6) & 0x3F);
        buf[2] = 0x80 | (code & 0x3F);
        return 3;
    } else if (!(code & ~0x1FFFFF)) {
        buf[0] = 0xF0 | (code >> 18);
        buf[1] = 0x80 | ((code >> 12) & 0x3F);
        buf[2] = 0x80 | ((code >> 6) & 0x3F);
        buf[3] = 0x80 | (code & 0x3F);
        return 4;
    } else if (!(code & ~0x3FFFFFF)) {
        buf[0] = 0xF8 | (code >> 24);
        buf[1] = 0x80 | ((code >> 18) & 0x3F);
        buf[2] = 0x80 | ((code >> 12) & 0x3F);
        buf[3] = 0x80 | ((code >> 6) & 0x3F);
        buf[4] = 0x80 | (code & 0x3F);
        return 5;
    } else if (!(code & ~0x7FFFFFFF)) {
        buf[0] = 0xFC | (code >> 30);
        buf[1] = 0x80 | ((code >> 24) & 0x3F);
        buf[2] = 0x80 | ((code >> 18) & 0x3F);
        buf[3] = 0x80 | ((code >> 12) & 0x3F);
        buf[4] = 0x80 | ((code >> 6) & 0x3F);
        buf[5] = 0x80 | (code & 0x3F);
        return 6;
    } else {
        throw Error("Invalid unicode codepoint %ud", code);
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
    return Datum(std::move(object));
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
    double number = strtod(buf, &end);
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
        out.printf("%.17lg", number);
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
