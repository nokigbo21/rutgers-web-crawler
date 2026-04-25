#ifndef IPC_SERVER_H
#define IPC_SERVER_H

typedef struct {
    unsigned int docid;
    unsigned int depth;
    char         url[2048];
    char         filepath[1024];
} doc_meta_t;

/* Create and bind UNIX domain socket, wait for one connection.
   Returns connected client fd, or -1 on error. */
int ipc_server_init(const char *sock_path);

/* Read next message into meta.
   Returns 1 on success, 0 on shutdown message, -1 on error/EOF. */
int ipc_recv_doc(int fd, doc_meta_t *meta);

void ipc_server_close(int server_fd, int client_fd, const char *sock_path);

#endif
