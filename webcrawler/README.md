# Multithreaded Web Crawler Pipeline

A concurrent web crawler + indexer + query tool written in C (C11).

---

## Build

**Dependencies:** `gcc`, `libcurl` (dev headers), `pthread` (standard on Linux).

```bash
# Install libcurl on Debian/Ubuntu:
sudo apt install libcurl4-openssl-dev

make all          # builds: crawler, indexer, query
make clean        # removes binaries and object files
make run          # end-to-end demo (see Makefile for tuning vars)
```

---

## Usage

```
# 1. Start indexer first (listens for crawler connection)
./indexer --ipc /tmp/crawl.sock --out data/index

# 2. In another terminal, start the crawler
./crawler --seed https://en.wikipedia.org/wiki/Linux \
          --max-depth 3 --max-pages 500 \
          -t 12 --out data --ipc /tmp/crawl.sock

# 3. After crawl finishes, query the index
./query --index data/index operating systems threads
```

### CLI Reference

```
crawler --seed <url> --max-depth <D> --max-pages <N> -t <threads> --out <dir> --ipc <path>
indexer --ipc <path> --out <dir>
query   --index <dir> <term1> [term2 ...]
```

---

## Architecture

### Crawler

**Thread pool** (`src/crawler/main.c`)  
A fixed-size pool of `pthread` worker threads is created at startup. Threads are never killed and reused until the crawl is complete. No thread-per-URL model is used.

**Bounded URL queue** (`src/crawler/queue.c` / `queue.h`)  
A singly-linked FIFO protected by a `pthread_mutex_t`. Two condition variables (`not_empty`, `not_full`) implement backpressure:
- Producers (`queue_push`) block when the queue is at capacity.
- Consumers (`queue_pop`) block when the queue is empty.
- `queue_shutdown()` broadcasts on both conditions so all threads wake and drain gracefully.

**Visited set** (`src/crawler/visited.c` / `visited.h`)  
Open-addressing hash set with djb2 hashing. The check-then-insert operation is atomic under a single mutex, preventing race conditions where two threads might both decide a URL is unvisited. The table auto-resizes when load exceeds 60%.

**Fetching** (`src/crawler/fetch.c`)  
Each worker calls `fetch_url()` which uses a per-call `CURL` easy handle. Timeouts (15 s total, 10 s connect), redirect following (max 5), and HTTP 2xx checking are all handled. Non-2xx responses and `libcurl` errors return `NULL`; the worker logs the failure and continues.

**Stop conditions**  
- `--max-pages`: An `atomic_int` counter tracks pages fetched. Once it reaches the limit any worker that would exceed it calls `queue_shutdown()` and exits.  
- `--max-depth`: Workers only enqueue child links when `depth < max_depth`.  
- Frontier exhaustion: `queue_pop()` returns -1 when the queue is empty and shutdown has been signalled.

**Persistence**  
Fetched HTML is written to `<out>/pages/<docid>.html` using a unique docid from an `atomic_uint` counter.

---

### IPC Protocol (`src/common/ipc_proto.h`)

Crawler and indexer communicate over a **UNIX domain socket** (`SOCK_STREAM`).

**Message format** (binary, network byte order):

| Field      | Type       | Description                         |
|------------|------------|-------------------------------------|
| `magic`    | uint32     | `0xCAFEBABE` – sanity check         |
| `docid`    | uint32     | Unique document ID                  |
| `depth`    | uint32     | Crawl depth                         |
| `url_len`  | uint16     | Length of URL string (incl. NUL)    |
| `path_len` | uint16     | Length of filepath (incl. NUL)      |
| _url_      | char[]     | `url_len` bytes                     |
| _filepath_ | char[]     | `path_len` bytes                    |

A **shutdown message** is a header-only message with `docid = 0xFFFFFFFF` and `url_len = path_len = 0`, sent by the crawler after all pages are fetched.

The indexer binds/listens on the socket path before the crawler starts, then `accept()`s one connection. All writes are serialised on the crawler side with a `pthread_mutex_t`.

---

### Indexer (`src/indexer/`)

Receives metadata via IPC, reads each saved HTML file, strips tags (inline), and tokenizes the text into lowercase alphanumeric words (≥ 2 chars). Each word is looked up or created in an in-memory hash table mapping `term → postings list`. After the shutdown message is received, the full index is flushed to disk.

---

### On-Disk Index Format

```
data/
├── pages/
│   ├── 0.html
│   ├── 1.html
│   └── ...
└── index/
    ├── docs.tsv       # docid TAB url TAB filepath TAB depth
    ├── dict.tsv       # term TAB byte_offset TAB df
    └── postings.bin   # binary: for each term: [uint32 df][uint32 docid * df]
```

- **`docs.tsv`** – human-readable document map, one record per line.
- **`dict.tsv`** – sorted dictionary. `byte_offset` is the byte position of the term's postings record in `postings.bin`. `df` is document frequency.
- **`postings.bin`** – each record begins with a `uint32` count followed by that many `uint32` docids (all little-endian / native byte order).

The query tool binary-searches `dict.tsv` for each query term, seeks into `postings.bin` to read the postings list, and computes the AND intersection across all terms.

---

## Summary of Stop Conditions (race-free)

| Condition | Mechanism |
|---|---|
| `--max-pages` reached | `atomic_int` counter; first thread to hit limit calls `queue_shutdown()` |
| `--max-depth` | Depth tracked per URL; no child links enqueued beyond limit |
| Frontier exhausted | `queue_pop()` returns -1 when empty + shutdown flag set |
| Worker thread exit | All threads join in `main()` before sending IPC shutdown |
