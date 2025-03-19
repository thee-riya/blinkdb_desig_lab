# Use Bash as the shell
SHELL := /bin/bash

# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++11 -pthread -Wall -O2

# Target executable and source file
TARGET := part2
SRC := part2.cpp

# Default target: build the executable
all: $(TARGET)

$(TARGET): $(SRC)
	@$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Run the server (runs silently)
run: $(TARGET)
	@./$(TARGET) >/dev/null 2>&1

benchmark: $(TARGET)
	@echo "Starting server in background..."; \
	fuser -k 9001/tcp >/dev/null 2>&1 || true; \
	SERVER_PID=$$(./$(TARGET) >/dev/null 2>&1 & echo $$!) ; \
	sleep 2 ; \
	for req in 10000 100000 1000000; do \
		for con in 10 100 1000; do \
			output_file="benchmark_$${req}_$${con}.txt"; \
			( echo "--------------------------------------------------"; \
			  echo "Benchmark: $$req requests, $$con parallel connections"; \
			  echo "Commands: SET & GET (without pipelining)"; \
			  echo "--------------------------------------------------"; \
			  redis-benchmark -t set,get -n $$req -c $$con -P 1 -p 9001 2>&1; ) > $$output_file; \
		done; \
	done ; \
	echo "Killing server with PID $$SERVER_PID"; \
	kill $$SERVER_PID >/dev/null 2>&1 ; \
	echo "all reports generated"

# Clean up build artifacts and generated benchmark reports.
clean:
	@rm -f $(TARGET) *.o benchmark_*.txt

.PHONY: all run benchmark clean
