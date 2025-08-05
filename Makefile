CC = clang
LD = $(CC)
CFLAGS = -O3 -march=native -Wall -Wextra -std=c23 -Iexternal/tree-sitter/lib/include -Iexternal/tomlc17/src -Iexternal/cjson
LDFLAGS = -lm
SRC_DIR = src
SRCS = $(wildcard $(SRC_DIR)/*.c)
BUILD_DIR = build
OBJS = $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%.o,$(notdir $(SRCS)))) $(BUILD_DIR)/tomlc17.o $(BUILD_DIR)/cJSON.o
EXEC_NAME = arc
TARGET = $(BUILD_DIR)/$(EXEC_NAME)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man

# Tree-sitter paths
TREE_SITTER_DIR = external/tree-sitter
TREE_SITTER_LIB = $(TREE_SITTER_DIR)/libtree-sitter.a

# tomlc17 paths
TOMLC17_DIR = external/tomlc17/src
TOMLC17_SRC = $(TOMLC17_DIR)/tomlc17.c

# cJSON paths
CJSON_DIR = external/cjson
CJSON_SRC = $(CJSON_DIR)/cJSON.c

.PHONY: all submodules
all: submodules $(BUILD_DIR) $(TARGET)

submodules:
	git submodule init
	git submodule update --recursive

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build tree-sitter static library
$(TREE_SITTER_LIB): submodules
	cd $(TREE_SITTER_DIR) && $(MAKE) CC=$(CC)

# Build tomlc17 object
$(BUILD_DIR)/tomlc17.o: $(TOMLC17_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build cJSON object
$(BUILD_DIR)/cJSON.o: $(CJSON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(TREE_SITTER_LIB)
	$(LD) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	cd $(TREE_SITTER_DIR) && $(MAKE) clean 2>/dev/null || true

.PHONY: install
install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(TARGET) $(DESTDIR)$(BINDIR)
	chmod +x $(DESTDIR)$(BINDIR)/$(EXEC_NAME)
	# mkdir -p $(DESTDIR)$(PREFIX)/share/arc
	# cp default.toml $(DESTDIR)$(PREFIX)/share/arc/default.toml

.PHONY: run
run: $(TARGET)
	./$(TARGET)

.PHONY: compile_commands
compile_commands:
	@echo '[' > compile_commands.json.tmp
	@first=true; \
	for src in $(SRCS) $(TOMLC17_SRC) $(CJSON_SRC); do \
		if [ "$first" = "true" ]; then \
			first=false; \
		else \
			echo ',' >> compile_commands.json.tmp; \
		fi; \
		echo '  {' >> compile_commands.json.tmp; \
		echo '    "directory": "$(shell pwd)",' >> compile_commands.json.tmp; \
		echo '    "command": "$(CC) $(CFLAGS) -o build/$(notdir $src).o -c $src",' >> compile_commands.json.tmp; \
		echo '    "file": "'$src'",' >> compile_commands.json.tmp; \
		echo '    "output": "build/$(notdir $src).o"' >> compile_commands.json.tmp; \
		echo '  }' >> compile_commands.json.tmp; \
	done
	@echo ']' >> compile_commands.json.tmp
	@mv compile_commands.json.tmp compile_commands.json
