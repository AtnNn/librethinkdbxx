# RethinkDB driver for C++

This driver is compatible with RethinkDB 2.0. It is based on the
official RethinkDB Python driver.

* [RethinkDB server](http://rethinkdb.com/)
* [RethinkDB API docs](http://rethinkdb.com/api/python/)

## Example

```
#include <memory>
#include <cstdio>
#include <rethinkdb.h>

namespace R = RethinkDB;

int main() {
  std::unique_ptr<R::Connection> conn = R::connect("localhost", 28015);
  R::Cursor c = R::table("users").filter(R::row["age"] > 14).run(*conn);
  while (c.has_next()) {
    R::Datum user = c.next();
    printf("%s\n", R::write_datum(user).c_str());
  }
}
```

## Build

Requires a modern C++ compiler. to build, run:

```
make
```

Will build `include/rethinkdb.h`, `librethinkdb++.a` and
`librethinkdb++.so` into the `build/` directory.

## Status

Still in early stages of development.

## Tests

This driver is tested against the upstream ReQL tests from the
RethinkDB repo, which are programmatically translated from Python to
C++. As of 34dc13c, all tests pass:

```
$ make test
...
SUCCESS: 2053 tests passed
```
