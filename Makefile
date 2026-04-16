CC=gcc
CFLAGS=-Wall -Wextra -std=c11

BUILD=build
EXE=$(BUILD)/cinc

SRC=$(shell find src -name '*.c')
OBJ=$(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
DEP=$(OBJ:.o=.d)

all: debug

debug: CFLAGS += -g
debug: $(EXE)

release: CFLAGS += -O3 -DNDEBUG
release: $(EXE)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(EXE): $(OBJ)
	$(CC) -o $@ $^

-include $(DEP)

clean:
	rm -rf $(BUILD)

test: $(EXE)
	@bash tests/test_runner.sh

run: all
	@$(EXE)
