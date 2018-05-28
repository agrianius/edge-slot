sources = edge_slot.cc
ut_sources = edge_slot_ut.cc main_ut.cc

objects = $(sources:.cc=.o)
ut_objects = $(ut_sources:.cc=.o)

depends = $(sources:.cc=.d)
ut_depends = $(ut_sources:.cc=.d)

all: lib

libedge-slot.a: deps $(objects)
	ar rcs $@ $(objects)

lib: libedge-slot.a

deps: $(depends) $(ut_depends)

%.d: %.cc
	$(CXX) -MM -std=c++14 -pthread -Wall -Wextra -O0 -ggdb $< -o $@.temp
	mv -f $@.temp $@

include $(sources:.cc=.d)
include $(ut_sources:.cc=.d)

%.o: %.cc
	$(CXX) -pthread -std=c++14 -Wall -Wextra -O0 -ggdb -c $< -o $@.temp
	mv -f $@.temp $@

test: lib $(ut_objects)
	$(CXX) $(ut_objects) libedge-slot.a -pthread -std=c++14 -lCppUTest -lCppUTestExt -Wall -Wextra -o $@

clean:
	rm -f *.d *.o libedge-slot.a test
