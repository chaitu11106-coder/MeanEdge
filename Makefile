# ============================================================================
# Makefile for Mini Algorithmic Trading Engine
# Production-grade build configuration
# ============================================================================

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pedantic
TARGET = trading_engine
SOURCES = main.cpp
HEADERS = trading_engine.hpp json_parser.hpp

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(SOURCES) $(HEADERS)
	@echo "Building trading engine..."
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)
	@echo "Build complete: ./$(TARGET)"

# Run with default data
run: $(TARGET)
	@echo "Running trading simulation..."
	./$(TARGET) market_data.json

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(TARGET)
	@echo "Clean complete"

# Debug build
debug: CXXFLAGS += -g -DDEBUG
debug: clean $(TARGET)

# Help
help:
	@echo "Mini Algorithmic Trading Engine - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build the trading engine"
	@echo "  make run      - Build and run with default market data"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make debug    - Build with debug symbols"
	@echo "  make help     - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  ./trading_engine <json_file>"

.PHONY: all run clean debug help
