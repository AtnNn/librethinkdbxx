#pragma once

#include "datum.h"
#include "stream.h"

namespace RethinkDB {

Datum read_datum(BufferedInputStream&);
Datum read_datum(const std::string&);
void write_datum(const Datum&, OutputStream&);
std::string write_datum(const Datum&);

}
