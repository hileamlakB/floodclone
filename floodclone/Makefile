# Compiler
CXX = g++
O?=0
# -fsanitize=thread 
CXXFLAGS = -std=c++17 -Iinclude -Wall -Wextra -pthread -g
LINKEDBINARIES = -lssl -lcrypto

# Source and binary files
SRC = src/*.cpp
TARGET = floodclone

# Build target
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LINKEDBINARIES) -O$(O)

# Clean target
.PHONY: clean
clean:
	rm -f $(TARGET)
