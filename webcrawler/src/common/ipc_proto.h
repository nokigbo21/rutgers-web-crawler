#ifndef IPC_PROTO_H
#define IPC_PROTO_H

#include <stdint.h>

/* Message sent from crawler -> indexer over UNIX domain socket.
   Fixed-size header + variable-length url/filepath strings. */

#define IPC_MSG_MAGIC  0xCAFEBABE
#define IPC_MAX_URL    2048
#define IPC_MAX_PATH   1024

typedef struct {
    uint32_t magic;       /* IPC_MSG_MAGIC */
    uint32_t docid;
    uint32_t depth;
    uint16_t url_len;     /* bytes, including NUL */
    uint16_t path_len;    /* bytes, including NUL */
    /* followed by url_len bytes then path_len bytes */
} ipc_msg_hdr_t;

/* Special docid sent to tell indexer to shut down */
#define IPC_SHUTDOWN_DOCID  0xFFFFFFFF

#endif /* IPC_PROTO_H */
