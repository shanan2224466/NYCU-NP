CXX := g++
CXXFLAG := -g -Wall -O3 -std=c++17
ROUTE := bin/
# NEVER MISS USE THE PATH BY WRITTING "PATH := bin/".
# OTHERWISE ALL THE COMMANDS WILL NOT BE AVAILABLE.
OBJS := npshell.o noop.o number.o removetag.o removetag0.o
TARGET := npshell

.PONY: clean all
all: $(TARGET) npshell.o

npshell: npshell.o
	$(CXX) $(CXXFLAG) $^ -o $@
npshell.o: $(ROUTE)npshell.cpp
	$(CXX) $(CXXFLAG) $< -c
noop.o: $(ROUTE)noop.cpp
	$(CXX) $(CXXFLAG) $< -c
number.o: $(ROUTE)number.cpp
	$(CXX) $(CXXFLAG) $< -c
removetag.o: $(ROUTE)removetag.cpp
	$(CXX) $(CXXFLAG) $< -c
removetag0.o: $(ROUTE)removetag0.cpp
	$(CXX) $(CXXFLAG) $< -c
clean:
	rm -f $(TARGET) $(OBJS)