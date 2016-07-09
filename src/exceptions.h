#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

class TimeoutException : public std::exception {
public:
    const char *what() const throw () { return "operation timed out"; }
};

#endif  // EXCEPTIONS_H
