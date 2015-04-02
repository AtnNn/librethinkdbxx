CXX ?= g++
CXXFLAGS ?= -std=c++11 -g -I'build/inc' -Wall

modules = net datum

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

%/.:
	mkdir -p $*

-include $(d_files)
