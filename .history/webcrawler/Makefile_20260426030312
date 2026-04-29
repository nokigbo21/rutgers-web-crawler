CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread -std=c11 \
           -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS = -pthread

CRAWLER_SRCS = src/crawler/main.c \
               src/crawler/queue.c \
               src/crawler/visited.c \
               src/crawler/fetch.c \
               src/crawler/parse.c \
               src/crawler/ipc_client.c

INDEXER_SRCS = src/indexer/main.c \
               src/indexer/tokenizer.c \
               src/indexer/ipc_server.c

QUERY_SRCS   = src/query/main.c \
               src/query/index_reader.c

CRAWLER_OBJS = $(CRAWLER_SRCS:.c=.o)
INDEXER_OBJS = $(INDEXER_SRCS:.c=.o)
QUERY_OBJS   = $(QUERY_SRCS:.c=.o)

all: crawler indexer query

crawler: $(CRAWLER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lcurl

indexer: $(INDEXER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

query: $(QUERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -Isrc/common -c $< -o $@

# Convenience: run a small end-to-end test
SEED    ?= https://en.wikipedia.org/wiki/Operating_system
DEPTH   ?= 3
PAGES   ?= 20
THREADS ?= 4
IPC     ?= /tmp/crawl.sock
OUT     ?= data
INDEX   ?= data/index
QUERY   ?= operating system kernel

run: all
	@echo "==> Starting indexer in background..."
	@mkdir -p $(INDEX)
	./indexer --ipc $(IPC) --out $(INDEX) &
	@sleep 1
	@echo "==> Starting crawler..."
	./crawler --seed $(SEED) --max-depth $(DEPTH) --max-pages $(PAGES) \
	          -t $(THREADS) --out $(OUT) --ipc $(IPC)
	@sleep 1
	@echo "==> Querying index..."
	./query --index $(INDEX) $(QUERY)

clean:
	rm -f $(CRAWLER_OBJS) $(INDEXER_OBJS) $(QUERY_OBJS)
	rm -f crawler indexer query
	rm -rf data/pages data/index

.PHONY: all clean run
