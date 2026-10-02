CC=gcc
CFLAGS=-std=c23 -Wall -Wextra -Wpedantic

BUILD=build
EXE=$(BUILD)/cinc

SRC=$(shell find src -name '*.c')
OBJ=$(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
DEP=$(OBJ:.o=.d)

all: debug

debug: CFLAGS += -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(EXE)

release: CFLAGS += -O3 -DNDEBUG
release: $(EXE)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

-include $(DEP)

clean:
	rm -rf $(BUILD)

test: $(EXE)
	@bash tests/run_tests

run: all
	@$(EXE)
