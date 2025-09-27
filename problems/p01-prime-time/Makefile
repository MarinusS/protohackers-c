# Problem-specific sources
SRC    := src/main.c
OBJS   := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRC))
TARGET := $(BIN_DIR)/p01-prime-time

# CC/CFLAGS come from top-level via command line
LDFLAGS :=

.PHONY: all run clean
all: $(TARGET)

# Link problem objects with shared objects (cJSON.o, utils.o, etc.)
$(TARGET): $(OBJS) $(LIB_OBJ)
	mkdir -p $(dir $@)
	$(CC) $(OBJS) $(LIB_OBJ) -o $@ $(LDFLAGS)

# Compile this problem sources into its own obj dir; also emit .d files
$(OBJ_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

-include $(OBJS:.o=.d)

run: $(TARGET)
	$(TARGET)

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET)

