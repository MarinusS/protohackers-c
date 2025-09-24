# --- Config ---
PROBLEMS := p00-smoke
BIN_DIR  := build/bin
OBJ_DIR  := build/obj
LIB_DIR  := build/lib

ROOT    := $(CURDIR)
BIN_DIR_ABS := $(abspath $(BIN_DIR))
OBJ_DIR_ABS := $(abspath $(OBJ_DIR))

# Includes (absolute) so sub-makes can compile from any cwd
INC := -I$(ROOT)/lib/include -I$(ROOT)/third_party/cjson

# Compiler flags (yours + includes + depfiles)
CC      := clang
CFLAGS  := -Wall -Wextra -std=c11 -g -MMD -MP $(INC)

# --- Shared objects (built once) ---
# cJSON
CJSON_SRC := third_party/cjson/cJSON.c
CJSON_OBJ := $(OBJ_DIR)/third_party/cjson/cJSON.o

# your utils
UTILS_SRC := lib/util/utils.c
UTILS_OBJ := $(OBJ_DIR)/lib/util/utils.o

LIB_OBJ := $(CJSON_OBJ) $(UTILS_OBJ)
LIB_OBJ_ABS := $(abspath $(LIB_OBJ))  # pass absolute to sub-makes

# Dep files for shared objs
LIB_DEPS := $(LIB_OBJ:.o=.d)

.PHONY: all $(PROBLEMS) clean
all: $(PROBLEMS)

# Generic compile rule for any .c -> build/obj/%.o (creates .d, too)
$(OBJ_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Build shared objects first, then delegate
$(PROBLEMS): %: $(LIB_OBJ)
	$(MAKE) -C problems/$@ \
	  BIN_DIR=$(BIN_DIR_ABS) \
	  OBJ_DIR=$(OBJ_DIR_ABS)/problems/$@ \
	  LIB_OBJ="$(LIB_OBJ_ABS)" \
	  CFLAGS="$(CFLAGS)"

-include $(LIB_DEPS)

clean:
	rm -rf build
	find . -name '*.o' -o -name '*.d' -delete

