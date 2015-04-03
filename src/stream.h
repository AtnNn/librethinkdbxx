#pragma once

#include <unistd.h>

#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <string>

#include "error.h"

namespace RethinkDB {

class BufferedInputStream {
public:
    BufferedInputStream(size_t buffer_size_ = 4096) :
        buffer_size(buffer_size_), pos(0), count(0), end(false) {
        buffer.reserve(buffer_size);
    }

    BufferedInputStream(const std::string& string) :
        buffer_size(string.size()), pos(0), count(string.size()), end(false) {
        buffer.assign(string.begin(), string.end());
    }

    int peek() {
        if (pos < count) {
            return buffer[pos];
        }
        if (end) {
            return EOF;
        }
        count = read_chunk(buffer.data(), buffer_size);
        pos = 0;
        if (count == 0) {
            end = true;
            return EOF;
        }
        return buffer[0]; 
    }

    int next() {
        int c = peek();
        pos++;
        return c;
    }

    bool eof() {
        return end;
    }

    virtual size_t read_chunk(char*, size_t) {
        return 0;
    }

    size_t buffer_size;
    std::vector<char> buffer;
    size_t pos;
    size_t count;
    bool end;
};

class OutputStream {
public:
    virtual void write(const char*, size_t) = 0;

    virtual void printf(const char* format, ...) {
        const size_t buffer_size = 4096;
        char buf[buffer_size];
        va_list args;
        va_start(args, format);
        int count = vsnprintf(buf, buffer_size, format, args);
        if (count == buffer_size) {
            throw Error("Output stream buffer too small");
        }
        va_end(args);
        write(buf, count);
    }

    void write(const std::string& string) {
        write(string.data(), string.size());
    }

    void write(const char* string) {
        write(string, strlen(string));
    }
};

class OutputBuffer : public OutputStream {
public:
    void write(const char* string, size_t size) {
        buffer.append(string, size);
    }

    std::string buffer;
};

class OutputFileDescriptor : public OutputStream {
public:
    void write(const char* buf, size_t size) {
        ::write(fd, buf, size);
    }

    OutputFileDescriptor(int fd_) : fd(fd_) { }

private:
    int fd;
};

}
