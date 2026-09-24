CXX = clang++
CXXFLAGS = -std=c++20 -Wall -Werror -Wextra -Wpedantic -g3

.PHONY: all run clean

all: Dinosaur

Dinosaur: Dinosaur.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

run: Dinosaur
	./Dinosaur

clean:
	rm -f Dinosaur
	rm -rf Dinosaur.dSYM
