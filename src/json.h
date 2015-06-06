#pragma once

#include "datum.h"
#include "stream.h"

namespace RethinkDB {

// Read a datum or throw an error
Datum read_datum(BufferedInputStream&);
Datum read_datum(const std::string&);

// Write a datum as JSON
void write_datum(const Datum&, OutputStream&);
std::string write_datum(const Datum&);

}
