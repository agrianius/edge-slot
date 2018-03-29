sources = $(wildcard *.cc)

objects = $(sources:.cc=.o)
depends = $(sources:.cc=.d)

all: $(objects)
deps: $(depends)

%.d: %.cc
	$(CXX) -MM -std=c++14 -pthread -lCppUTest -lCppUTestExt -Wall -Wextra -O2 $< -o $@.temp
	mv -f $@.temp $@

include $(sources:.cc=.d)

%.o: %.cc
	$(CXX) -pthread -std=c++14 -Wall -Wextra -O2 -c $< -o $@.temp
	mv -f $@.temp $@

test: $(objects)
	$(CXX) $(objects) -pthread -std=c++14 -lCppUTest -lCppUTestExt -Wall -Wextra -o test

clean:
	rm -f *.d *.o test
