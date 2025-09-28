# Infer problem name from directory name
PROBLEM ?= $(notdir $(CURDIR))
BIN     := $(BIN_DIR)/$(PROBLEM)

# Sources for this problem
SRC   := $(wildcard src/*.c)
OBJS  := $(SRC:%.c=$(OBJ_DIR)/%.o)

.PHONY: all clean run
all: $(BIN)

# Link with shared objs provided by top-level (cJSON.o, utils.o, etc.)
$(BIN): $(OBJS) $(LIB_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile each .c into this problem's obj dir; emit dep files via -MMD -MP (already in CFLAGS)
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

-include $(OBJS:.o=.d)

run: $(BIN)
	$(BIN)

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(BIN)
