# --- Config ---
PROBLEMS := p00-smoke
BIN_DIR  := build/bin
OBJ_DIR  := build/obj
LIB_DIR  := build/lib

ROOT        := $(CURDIR)
BIN_DIR_ABS := $(abspath $(BIN_DIR))
OBJ_DIR_ABS := $(abspath $(OBJ_DIR))

# Includes (absolute) so sub-makes can compile from any cwd
INC := -I$(ROOT)/lib/include -I$(ROOT)/third_party/cjson

# Compiler/linker
CC      := clang
CFLAGS  := -Wall -Wextra -std=c11 -g -MMD -MP $(INC)
LDFLAGS :=

# --- Shared objects (built once) ---
# cJSON
CJSON_SRC := third_party/cjson/cJSON.c
CJSON_OBJ := $(OBJ_DIR)/third_party/cjson/cJSON.o

# your utils
UTILS_SRC := lib/util/utils.c
UTILS_OBJ := $(OBJ_DIR)/lib/util/utils.o

LIB_OBJ     := $(CJSON_OBJ) $(UTILS_OBJ)
LIB_OBJ_ABS := $(abspath $(LIB_OBJ))  # pass absolute to sub-makes
LIB_DEPS    := $(LIB_OBJ:.o=.d)

# --- Debug helpers ---
ASAN        := -fsanitize=address,undefined
DBG_CFLAGS  := -O0 -g3 -fno-omit-frame-pointer $(ASAN)
DBG_LDFLAGS := $(ASAN)

# Which binary to debug: make gdb DBG=p00-smoke
DBG     ?= $(firstword $(PROBLEMS))
DBG_BIN := $(BIN_DIR)/$(DBG)

.PHONY: all $(PROBLEMS) clean debug gdb gdb-%

all: $(PROBLEMS)

# Generic compile rule for any .c -> build/obj/%.o (creates .d, too)
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Build shared objects first, then delegate to each problem's Makefile
$(PROBLEMS): %: $(LIB_OBJ)
	$(MAKE) -C problems/$@ \
	  BIN_DIR=$(BIN_DIR_ABS) \
	  OBJ_DIR=$(OBJ_DIR_ABS)/problems/$@ \
	  LIB_OBJ="$(LIB_OBJ_ABS)" \
	  CFLAGS="$(CFLAGS)" \
	  LDFLAGS="$(LDFLAGS)"

-include $(LIB_DEPS)

# Debug build of everything (adds ASan/UBSan and no optimizations)
debug: CFLAGS += $(DBG_CFLAGS)
debug: LDFLAGS += $(DBG_LDFLAGS)
debug: all

# Launch GDB on one problem binary after a debug build
gdb: debug $(DBG_BIN)
	@echo "GDB → $(DBG_BIN)"
	gdb --quiet \
	    -ex "set pagination off" \
	    -ex "handle SIGPIPE nostop noprint pass" \
	    -ex "break main" \
	    -ex "run" \
	    --args $(DBG_BIN)

# Convenience: make gdb-p00-smoke
gdb-%: DBG:=$*
gdb-%: gdb

clean:
	rm -rf build
	find . -name '*.o' -o -name '*.d' -delete
