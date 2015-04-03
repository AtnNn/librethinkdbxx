CXX ?= g++
CXXFLAGS ?= -std=c++11 -g -I'build/inc' -Wall

modules = net datum json query
headers = error stream datum json net query

o_files = $(patsubst %, build/obj/%.o, $(modules))
d_files = $(patsubst %, build/dep/%.d, $(modules))

build/librethinkdb++.a: $(o_files)
	ar rcs $@ $^

build/librethink++.so: $(o_files)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^

build/obj/%.o: src/%.cc build/inc/protocol_defs.h
	@mkdir -p $(dir $@)
	@mkdir -p $(dir build/dep/$*.d)
	$(CXX) $(CXXFLAGS) -c -o $@ $< -MP -MQ $@ -MD -MF build/dep/$*.d

build/inc/protocol_defs.h: reql/ql2.proto reql/gen.py | build/inc/.
	python reql/gen.py $< > $@

clean:
	rm -rf build

build/include/rethinkdb.h: build/inc/protocol_defs.h $(patsubst %, src/%.h, $(headers)) | build/include/.
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
