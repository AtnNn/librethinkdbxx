CXX = clang++
CXXFLAGS = -std=c++11 -g -I'build/gen' -Wall -pthread

modules = net datum json query cursor types utils
headers = utils error stream types datum json net cursor query

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
	python3 reql/gen.py $< > $@

clean:
	rm -rf build

build/include/rethinkdb.h: build/gen/protocol_defs.h $(patsubst %, src/%.h, $(headers)) | build/include/.
	( echo "// Auto-generated file, built from $^"; \
	  echo '#pragma once'; \
	  cat $^ | \
	    grep -v '^#pragma once' | \
	    grep -v '^#include "'; \
	) > $@

build/gen/upstream_tests.cc: test/yaml_to_cxx.py
	python3 test/yaml_to_cxx.py test/upstream > $@

test_sources = test/testlib.cc test/test.cc build/gen/upstream_tests.cc
build/test: $(test_sources) test/testlib.h build/librethinkdb++.a build/include/rethinkdb.h
	$(CXX) $(CXXFLAGS) -o $@ -isystem build/include -I test $(test_sources) build/librethinkdb++.a

.PHONY: test
test: build/test
	build/test

%/.:
	mkdir -p $*

-include $(d_files)
