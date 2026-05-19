CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O2 -Iinclude
LDFLAGS :=

BIN := bin
SRC := src

SERVER := $(BIN)/file-server
CLIENT := $(BIN)/file-client

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
	EVENT_LOOP_OBJ := $(BIN)/event_loop_kqueue.o
else ifeq ($(UNAME_S),Linux)
	EVENT_LOOP_OBJ := $(BIN)/event_loop_epoll.o
else
	$(error Unsupported platform: $(UNAME_S))
endif

COMMON_OBJS := \
	$(BIN)/collections.o \
	$(BIN)/common.o \
	$(BIN)/format.o

all: $(SERVER) $(CLIENT)

$(BIN):
	mkdir -p $(BIN)

$(BIN)/%.o: $(SRC)/%.c | $(BIN)
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER): $(BIN)/server.o $(EVENT_LOOP_OBJ) $(COMMON_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(CLIENT): $(BIN)/client.o $(COMMON_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)

.PHONY: all clean
