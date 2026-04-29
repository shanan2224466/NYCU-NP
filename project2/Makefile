CXX := g++
CXXFLAG := -g -Wall -std=c++17
ROUTE := bin/

# NEVER MISS USE THE PATH BY WRITTING "PATH := bin/".
# OTHERWISE ALL THE COMMANDS WILL NOT BE AVAILABLE.

TARGET := np_simple np_single_proc noop number removetag removetag0

.PHONY: clean all

all: $(TARGET)

np_simple: np_simple.cpp
	$(CXX) $(CXXFLAG) $< -o $@

np_single_proc: np_single_proc.cpp
	$(CXX) $(CXXFLAG) $< -o $@

noop: $(ROUTE)noop.cpp
	$(CXX) $(CXXFLAG) $< -o $(ROUTE)$@

number: $(ROUTE)number.cpp
	$(CXX) $(CXXFLAG) $< -o $(ROUTE)$@

removetag: $(ROUTE)removetag.cpp
	$(CXX) $(CXXFLAG) $< -o $(ROUTE)$@

removetag0: $(ROUTE)removetag0.cpp
	$(CXX) $(CXXFLAG) $< -o $(ROUTE)$@

clean:
	$(RM) npshell $(ROUTE)noop $(ROUTE)number $(ROUTE)removetag $(ROUTE)removetag0
