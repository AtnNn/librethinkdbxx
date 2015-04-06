CXX := clang++
CXXFLAGS := -std=c++11 -g -I'build/gen' -Wall -pthread

modules := net datum json query cursor types utils
headers := utils error stream types datum json net cursor query

o_files := $(patsubst %, build/obj/%.o, $(modules))
d_files := $(patsubst %, build/dep/%.d, $(modules))

skip_tests := regression/1133 regression/767 regression/1005 # python-only
skip_tests += changefeeds/squash # double run
skip_tests += arity # arity errors are compile-time
skip_tests += times regression/1023 regression/2774 # time
skip_tests += geo # geo

# TODO
# test_filter = datum/binary
# test_filter = math_logic/comparison
# test_filter = math_logic/logic
# test_filter = control
# test_filter = arraylimits
# test_filter = regression/2838
skip_tests += regression/370

upstream_tests := $(filter-out $(patsubst %,test/upstream/%%, $(skip_tests)), $(filter test/upstream/$(test_filter)%,$(shell find test/upstream -name '*.yaml')))
upstream_tests_cc := $(patsubst %.yaml, build/tests/%.cc, $(upstream_tests))
upstream_tests_o := $(patsubst %.cc, %.o, $(upstream_tests_cc))

.PRECIOUS: $(upstream_tests_cc) $(upstream_tests_o)

default: build/librethinkdb++.a

all: build/librethinkdb++.a build/librethink++.so build/test build/include/rethinkdb.h

build/librethinkdb++.a: $(o_files)
	ar rcs $@ $^

build/librethink++.so: $(o_files)
	$(CXX) -o $@ $(CXXFLAGS) -shared $^

build/obj/%.o: src/%.cc build/gen/protocol_defs.h
	@mkdir -p $(dir $@)
	@mkdir -p $(dir build/dep/$*.d)
	$(CXX) -o $@ $(CXXFLAGS) -c $< -MP -MQ $@ -MD -MF build/dep/$*.d

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

build/tests/%.cc: %.yaml test/yaml_to_cxx.py
	@mkdir -p $(dir $@)
	python3 test/yaml_to_cxx.py $< > $@

build/tests/upstream_tests.cc: $(upstream_tests) test/gen_index_cxx.py FORCE | build/tests/.
	@echo 'python3 test/gen_index_cxx.py ... > $@'
	@python3 test/gen_index_cxx.py $(upstream_tests) > $@

build/tests/%.o: build/tests/%.cc build/include/rethinkdb.h test/testlib.h | build/tests/.
	$(CXX) -o $@ $(CXXFLAGS) -isystem build/include -I test -c $< -Wno-unused-variable

build/tests/%.o: test/%.cc test/testlib.h build/include/rethinkdb.h | build/tests/.
	$(CXX) -o $@ $(CXXFLAGS) -isystem build/include -I test -c $<

build/test: build/tests/testlib.o build/tests/test.o build/tests/upstream_tests.o $(upstream_tests_o) build/librethinkdb++.a
	@echo $(CXX) -o $@ $(CXXFLAGS) build/librethinkdb++.a ...
	@$(CXX) -o $@ $(CXXFLAGS) build/librethinkdb++.a $^ 

.PHONY: test
test: build/test
	build/test

%/.:
	mkdir -p $*

.PHONY: FORCE
FORCE:

-include $(d_files)
