CXX = g++
CXXFLAGS = -std=c++11 -g -I'build/gen' -Wall -pthread

modules = net datum json query cursor
headers = error stream datum json net cursor query

o_files = $(patsubst %, build/obj/%.o, $(modules))
d_files = $(patsubst %, build/dep/%.d, $(modules))

default: build/librethinkdb++.a

all: build/librethinkdb++.a build/librethink++.so build/test build/include/rethinkdb.h

build/librethinkdb++.a: $(o_files)
	ar rcs $@ $^

build/librethink++.so: $(o_files)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^

build/obj/%.o: src/%.cc build/gen/protocol_defs.h
	@mkdir -p $(dir $@)
	@mkdir -p $(dir build/dep/$*.d)
	$(CXX) $(CXXFLAGS) -c -o $@ $< -MP -MQ $@ -MD -MF build/dep/$*.d

build/gen/protocol_defs.h: reql/ql2.proto reql/gen.py | build/gen/.
	python reql/gen.py $< > $@

clean:
	rm -rf build

build/include/rethinkdb.h: build/gen/protocol_defs.h $(patsubst %, src/%.h, $(headers)) | build/include/.
	( echo "// Auto-generated file, built from $^"; \
	  echo '#pragma once'; \
	  cat $^ | \
	    grep -v '^#pragma once' | \
	    grep -v '^#include "'; \
	) > $@

build/test: test/test.cc build/librethinkdb++.a build/include/rethinkdb.h
	$(CXX) $(CXXFLAGS) -o $@ -isystem build/include $< build/librethinkdb++.a

test: build/test
	build/test

%/.:
	mkdir -p $*

-include $(d_files)
