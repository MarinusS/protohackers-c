SRC := $(wildcard src/*.c)
BIN := $(BIN_DIR)/$(notdir $(CURDIR))

.PHONY: all
all: $(BIN)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRC))
DEPS := $(OBJS:.o=.d)

$(BIN): $(OBJS) $(LIB_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OBJS) $(LIB_OBJ) -o $@ $(LDFLAGS)

-include $(DEPS)
