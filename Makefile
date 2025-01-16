.PHONY: all clean

all: esoplay

esoplay: main.cpp
	g++ -std=c++23 -Wall -Wextra -pedantic -o $@ $<

clean:
	rm -f esoplay