# Compiler
CXX = g++
CXXFLAGS = -std=c++17 -Iinclude -I/usr/local/opt/openssl/include -Wall -Wextra

# Source and binary files
SRC = src/main.cpp src/FileManager.cpp
TARGET = floodclone

# Build target
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# Clean target
.PHONY: clean
clean:
	rm -f $(TARGET)
