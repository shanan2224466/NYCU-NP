CXX := g++
CXXFLAG := -g -Wall -O3 -std=c++17
ROUTE := bin/
# NEVER MISS USE THE PATH BY WRITTING "PATH := bin/".
# OTHERWISE ALL THE COMMANDS WILL NOT BE AVAILABLE.

TARGET := npshell noop number removetag removetag0

.PONY: clean all
all: $(TARGET) npshell.o

npshell: $(ROUTE)npshell.cpp
	$(CXX) $(CXXFLAG) $< -o $@
noop: $(ROUTE)noop.cpp
	$(CXX) $(CXXFLAG) $< -o $@
number: $(ROUTE)number.cpp
	$(CXX) $(CXXFLAG) $< -o $@
removetag: $(ROUTE)removetag.cpp
	$(CXX) $(CXXFLAG) $< -o $@
removetag0: $(ROUTE)removetag0.cpp
	$(CXX) $(CXXFLAG) $< -o $@
clean:
	rm -f bin/npshell bin/noop bin/number bin/removetag bin/removetag0