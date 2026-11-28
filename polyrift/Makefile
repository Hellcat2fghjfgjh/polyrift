CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:src/%.cpp=build/%.o)
BIN      := polyrift

ifeq ($(OS),Windows_NT)
  LDLIBS  += -lws2_32
  LDFLAGS += -static
  BIN     := polyrift.exe
endif

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.cpp $(wildcard src/*.hpp) | build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build:
	mkdir -p build

run: $(BIN)
	./$(BIN)

test: $(BIN)
	./$(BIN) --selftest 3000

clean:
	rm -rf build $(BIN)

.PHONY: all run test clean
