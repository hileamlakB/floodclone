# Compiler
CXX = g++
# -fsanitize=thread 
CXXFLAGS = -std=c++17 -Iinclude -Wall -Wextra -pthread

# Source and binary files
SRC = src/*.cpp
TARGET = floodclone

# Build target
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# Clean target
.PHONY: clean
clean:
	rm -f $(TARGET)
