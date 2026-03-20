CC=gcc
CFLAGS=-Wall -Wextra -std=c11

BUILD=build
EXE=$(BUILD)/cinc

SRC=$(wildcard src/*.c)
OBJ=$(SRC:src/%.c=$(BUILD)/%.o)

all: debug

debug: CFLAGS += -g
debug: $(EXE)

release: CFLAGS += -O3 -DNDEBUG
release: $(EXE)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(EXE): $(OBJ)
	$(CC) -o $@ $^

clean:
	rm -rf $(BUILD)

run: all
	@$(EXE)
