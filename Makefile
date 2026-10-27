ifeq ($(OS),Windows_NT)
CXX ?= x86_64-w64-mingw32-g++
else
CXX ?= g++
endif
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Iinclude
TARGET ?= bin/gatochi.exe
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(SOURCES:src/%.cpp=bin/%.o)

SFML_MODULES := sfml-graphics sfml-window sfml-system sfml-audio
SFML_CFLAGS ?= $(shell pkg-config --cflags $(SFML_MODULES) 2>/dev/null)
SFML_LIBS ?= $(shell pkg-config --libs $(SFML_MODULES) 2>/dev/null)
ifeq ($(strip $(SFML_LIBS)),)
SFML_LIBS := -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio
endif

ifeq ($(OS),Windows_NT)
VIDEO_LIBS := -lmfplay -lmfplat -lmfuuid -lole32
MINGW_BIN := $(shell dirname "$(shell command -v $(CXX))")
endif

WINE ?= wine

ifeq ($(shell command -v wine 2>/dev/null),)
ifeq ($(strip $(DISPLAY)),)
ifneq ($(shell command -v xvfb-run 2>/dev/null),)
RUN_COMMAND ?= ALSOFT_DRIVERS=null xvfb-run -a ./$(TARGET)
else
ifeq ($(OS),Windows_NT)
RUN_COMMAND ?= PATH="$(MINGW_BIN):$$PATH" ./$(TARGET)
else
RUN_COMMAND ?= ./$(TARGET)
endif
endif
else
RUN_COMMAND ?= ./$(TARGET)
endif
else
RUN_COMMAND ?= $(WINE) $(TARGET)
endif

.PHONY: all build run test clean

all: build

build: $(TARGET)

ifeq ($(strip $(SOURCES)),)
$(TARGET):
	@printf '%s\n' 'No hay archivos fuente en src/; se omite la compilacion.'
else
$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SFML_CFLAGS) $^ -o $@ $(SFML_LIBS) $(VIDEO_LIBS)

bin/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SFML_CFLAGS) -c $< -o $@
endif

run: build
	$(RUN_COMMAND)

test: build
	$(RUN_COMMAND) --test

clean:
	rm -f $(OBJECTS) $(TARGET)