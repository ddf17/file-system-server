CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O2 -Iinclude
LDFLAGS :=

BIN := bin
SRC := src

SERVER := $(BIN)/file-server
CLIENT := $(BIN)/file-client

COMMON_OBJS := \
	$(BIN)/common.o \
	$(BIN)/format.o

all: $(SERVER) $(CLIENT)

$(BIN):
	mkdir -p $(BIN)

$(BIN)/%.o: $(SRC)/%.c | $(BIN)
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER): $(BIN)/server.o $(COMMON_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(CLIENT): $(BIN)/client.o $(COMMON_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)

.PHONY: all clean
