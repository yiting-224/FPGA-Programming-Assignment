CXX      ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra
LDFLAGS  ?=

SRC_DIR  := src
SRCS     := $(wildcard $(SRC_DIR)/*.cpp)
OBJS     := $(SRCS:.cpp=.o)
HDRS     := $(wildcard $(SRC_DIR)/*.hpp)
TARGET   := partitioner

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
