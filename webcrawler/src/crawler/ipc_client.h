#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

/* Connect to the indexer's UNIX domain socket.
   Returns fd >= 0 on success, -1 on failure. */
int ipc_connect(const char *sock_path);

/* Send one document metadata message.
   Returns 0 on success, -1 on failure. */
int ipc_send_doc(int fd, unsigned int docid, const char *url,
                 const char *filepath, unsigned int depth);

/* Send shutdown message so indexer knows crawl is done. */
int ipc_send_shutdown(int fd);

void ipc_close(int fd);

#endif /* IPC_CLIENT_H */
