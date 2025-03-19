# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++11 -pthread -Wall -O2

# Target executable name
TARGET := blinkdb

# Source file(s)
SRC := part1.cpp

# Default target: build the executable
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Run the program with the "part1" argument
run: $(TARGET)
	./$(TARGET) part1

# Clean up build artifacts
clean:
	rm -f $(TARGET) *.o

.PHONY: all run clean
