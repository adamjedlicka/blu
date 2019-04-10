# Makefile for building a single configuration of the C interpreter. It expects variables to be passed in for:
#
# MODE         "debug" or "release".

ifndef MODE
MODE := release
endif

NAME := blu
SOURCE_DIR := src

CFLAGS := -std=c99 -Wall -Wextra -Wno-unused-parameter

# Mode configuration.
ifeq ($(MODE),debug)
	CFLAGS += -O0 -DDEBUG -g
	BUILD_DIR := build/debug
else
	CFLAGS += -O3 -flto
	BUILD_DIR := build/release
endif

# Files.
HEADERS := $(wildcard $(SOURCE_DIR)/*.h)
SOURCES := $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS := $(addprefix $(BUILD_DIR)/$(NAME)/, $(notdir $(SOURCES:.c=.o)))

# Targets

# Link the interpreter.
build/$(NAME): $(OBJECTS)
	@ printf "%8s %-40s %s\n" $(CC) $@ "$(CFLAGS)"
	@ mkdir -p build
	@ $(CC) $(CFLAGS) $^ -o $@

# Compile object files.
$(BUILD_DIR)/$(NAME)/%.o: $(SOURCE_DIR)/%.c $(HEADERS)
	@ printf "%8s %-40s %s\n" $(CC) $< "$(CFLAGS)"
	@ mkdir -p $(BUILD_DIR)/$(NAME)
	@ $(CC) -c $(CFLAGS) -o $@ $<

run: build/$(NAME)
	./build/$(NAME) $(SCRIPT)

test: build/$(NAME)
	bash test.sh

clean:
	rm -rf build/

.PHONY: default
