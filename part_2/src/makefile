# Use Bash as the shell
SHELL := /bin/bash

# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++11 -pthread -Wall -O2

# Target executable name and source file
TARGET := part2
SRC := part2.cpp

# Default target: Build the executable, generate documentation, and run benchmark tests.
all: $(TARGET)
	@echo "Generating documentation..."
	@doxygen DoxyFile
	# @echo "Compiling PDF documentation..."
	# @cd ../docs/latex && make
	@echo "Running benchmark tests..."
	@$(MAKE) benchmark

$(TARGET): $(SRC)
	@$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Run the server silently
run: $(TARGET)
	@./$(TARGET) >/dev/null 2>&1

# Benchmark target:
#   - Creates a "result" folder.
#   - Starts the server in the background, runs benchmarks for all configurations,
#     storing each output in the "result" folder.
#   - Finally, kills the server and prints a final message.
benchmark: $(TARGET)
	@echo "Starting server in background..."
	@fuser -k 9001/tcp >/dev/null 2>&1 || true
	@SERVER_PID=$$(./$(TARGET) >/dev/null 2>&1 & echo $$!); \
	sleep 2; \
	mkdir -p ../result; \
	for req in 10000 100000 1000000; do \
		for con in 10 100 1000; do \
			output_file="../result/benchmark_$${req}_$${con}.txt"; \
			( echo "--------------------------------------------------"; \
			  echo "Benchmark: $$req requests, $$con parallel connections"; \
			  echo "Commands: SET & GET (without pipelining)"; \
			  echo "--------------------------------------------------"; \
			  redis-benchmark -t set,get -n $$req -c $$con -P 1 -p 9001 2>&1 ) > $$output_file; \
		done; \
	done; \
	echo "Killing server with PID $$SERVER_PID"; \
	kill $$SERVER_PID >/dev/null 2>&1; \
	echo "all reports generated"

# Clean up build artifacts and generated documentation/benchmark reports.
clean:
	@rm -f $(TARGET) *.o
	@rm -rf ../result ../docs/html docs/latex

.PHONY: all run benchmark clean
