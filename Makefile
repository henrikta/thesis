CXXFLAGS=-isystem ./stx-btree/include
SRC=tabulation.cpp performance_clock.cpp
DST= build/correctness_test
DST+=build/performance_test
DST+=build/hopscotch_experiment

all: ${DST}

clean:
	rm -rf build

build/%: %.cpp ${SRC} *.hpp
	mkdir -p build
	${CXX} -Wall -Wextra -O3 --std=c++14 ${CXXFLAGS} $< ${SRC} -o $@
