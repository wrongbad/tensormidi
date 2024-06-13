INCLUDES := $(shell python -m tensormidi.includes)

parse : parse.cpp
	c++ -g $^ -o $@ -I$(INCLUDES)
