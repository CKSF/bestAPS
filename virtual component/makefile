# Usage:
# make          # compile both programs
# make clean    # remove all binaries and objects

# Compiler and flags
CC = g++
CPPFLAGS = -pthread -Wall
LDFLAGS = -lpthread -lpaho-mqtt3as -lpaho-mqttpp3

# Programs
PROG = main
SRC = main.cpp
OBJ = $(SRC:.cpp=.o)

# Headers (assumes both programs share headers)
HEADERS = $(wildcard *.h)

# Default target to compile both programs
all: $(PROG)

# Rule to compile program1
$(PROG): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

# Rule to compile object files
%.o: %.cpp $(HEADERS)
	$(CC) $(CPPFLAGS) -c $< -o $@

# Clean up all object and binary files
clean:
	rm -rvf *.o $(PROG)