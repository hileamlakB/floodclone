# Compiler
CXX = g++
O ?= 0

# -fsanitize options for Thread and Memory
SAN_FLAGS = 
ifeq ($(SAN),1)
	# -fsanitize=address
    SAN_FLAGS =  -fsanitize=thread
endif

CXXFLAGS = -std=c++17 -Iinclude -Wall -Wextra -pthread -g $(SAN_FLAGS)
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
