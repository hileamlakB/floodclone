CXX = g++
O ?= 0
SAN_FLAGS = 
ifeq ($(SAN),1)
    SAN_FLAGS = -fsanitize=thread
endif

CXXFLAGS = -std=c++17 -Iinclude -Wall -Wextra -pthread -g $(SAN_FLAGS) -pg
LINKEDBINARIES = -lssl -lcrypto

SRC_DIR = src
TEST_DIR = tests
SRC = $(SRC_DIR)/*.cpp
TEST_SRC = $(TEST_DIR)/*.cpp

TARGET = floodclone
TEST_TARGET = test_floodclone

$(TARGET): floodclone.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) floodclone.cpp $(SRC) $(LINKEDBINARIES) -O$(O) 

test: $(TEST_SRC) $(SRC)
	$(CXX) $(CXXFLAGS) -DTESTING -o $(TEST_TARGET) $(TEST_SRC) $(SRC) $(LINKEDBINARIES) -O$(O)

.PHONY: clean all
clean:
	rm -f $(TARGET) $(TEST_TARGET)

all: $(TARGET) test