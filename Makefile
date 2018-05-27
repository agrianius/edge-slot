sources = edge_slot.cc
ut_sources = edge_slot_ut.cc main_ut.cc

objects = $(sources:.cc=.o)
ut_objects = $(ut_sources:.cc=.o)

depends = $(sources:.cc=.d)
ut_depends = $(ut_sources:.cc=.d)

all: lib

lib: deps $(objects)
	ar rcs libedge-slot.a $(objects)

deps: $(depends)

%.d: %.cc
	$(CXX) -MM -std=c++14 -pthread -Wall -Wextra -O2 $< -o $@.temp
	mv -f $@.temp $@

include $(sources:.cc=.d)

%.o: %.cc
	$(CXX) -pthread -std=c++14 -Wall -Wextra -O2 -c $< -o $@.temp
	mv -f $@.temp $@

test: lib $(ut_objects)
	$(CXX) $(ut_objects) libedge-slot.a -pthread -std=c++14 -lCppUTest -lCppUTestExt -Wall -Wextra -o test

clean:
	rm -f *.d *.o libedge-slot.a test
