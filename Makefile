CC = clang
LD = $(CC)
CFLAGS = -O3 -march=native -Wall -Wextra -std=c23 -Iexternal/tree-sitter/lib/include -Iexternal/tomlc17/src
LDFLAGS = -lm
SRC_DIR = src
SRCS = $(wildcard $(SRC_DIR)/*.c)
BUILD_DIR = build
OBJS = $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%.o,$(notdir $(SRCS)))) $(BUILD_DIR)/tomlc17.o
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
	@echo '[' > compile_commands.json
	@for src in $(SRCS) $(TOMLC17_SRC); do \
	echo '  {' >> compile_commands.json; \
	echo '	"directory": "$(shell pwd)",' >> compile_commands.json; \
	echo '	"command": "$(CC) $(CFLAGS) -c $$src",' >> compile_commands.json; \
	echo '	"file": "$$src"' >> compile_commands.json; \
	if [ "$$src" != "$(lastword $(SRCS) $(TOMLC17_SRC))" ]; then \
	echo '  },' >> compile_commands.json; \
	else \
	echo '  }' >> compile_commands.json; \
	fi; \
	done
	@echo ']' >> compile_commands.json
