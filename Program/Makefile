# Reproducible build profiles for the RKO.
OS := $(shell uname -s)

ifeq ($(OS),Darwin)
	CXX ?= g++-14
else
	CXX ?= g++
endif

COMMON_FLAGS := -fopenmp -Wall -Wextra -std=c++20 -Wno-unused-but-set-variable
PERFORMANCE_FLAGS := -O3 -DNDEBUG -march=native
DEBUG_FLAGS := -O0 -g

RKO_SOURCE := src/Main/main.cpp
RKO_HEADERS := $(shell find src -type f -name '*.h')
RKO_TARGET := runRKO

.PHONY: all release debug rebuild clean

all: release

release: $(RKO_SOURCE) $(RKO_HEADERS)
	$(CXX) $(COMMON_FLAGS) $(PERFORMANCE_FLAGS) $(RKO_SOURCE) -o $(RKO_TARGET)

debug: $(RKO_SOURCE) $(RKO_HEADERS)
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) $(RKO_SOURCE) -o $(RKO_TARGET)

rebuild: debug

clean:
	$(RM) $(RKO_TARGET) src/Main/main.o
