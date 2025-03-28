# Use Bash as the shell
SHELL := /bin/bash

# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++11 -pthread -Wall -O2

# Target executable name and source file
TARGET := blinkdb
SRC := part1.cpp

# Default target: Build the executable, generate documentation, generate report, and run the program.
all: $(TARGET)
	@echo "Generating documentation..."
	@doxygen DoxyFile
	@echo "Compiling PDF documentation..."
	@$(MAKE) -C ../docs/latex
	@echo "Renaming refman.pdf to report.pdf..."
	@mv ../docs/latex/refman.pdf ../docs/latex/report.pdf
	@echo "Running the program..."
	@./$(TARGET) part1

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Clean up build artifacts and documentation
clean:
	rm -f $(TARGET) *.o
	rm -rf ../docs/html ../docs/latex

.PHONY: all clean
