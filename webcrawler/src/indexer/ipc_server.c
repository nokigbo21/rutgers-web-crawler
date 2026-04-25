#include "ipc_server.h"
#include "../common/ipc_proto.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>

int ipc_server_init(const char *sock_path) {
    unlink(sock_path); /* remove stale socket */

    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sfd); return -1;
    }
    if (listen(sfd, 1) < 0) {
        perror("listen"); close(sfd); return -1;
    }

    printf("[IPC] Listening on %s, waiting for crawler...\n", sock_path);
    int cfd = accept(sfd, NULL, NULL);
    if (cfd < 0) { perror("accept"); close(sfd); return -1; }
    printf("[IPC] Crawler connected.\n");

    close(sfd); /* only one connection */
    return cfd;
}

static int read_all(int fd, void *buf, size_t n) {
    char *p = buf;
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r <= 0) return -1;
        p += r; n -= (size_t)r;
    }
    return 0;
}

int ipc_recv_doc(int fd, doc_meta_t *meta) {
    ipc_msg_hdr_t hdr;
    if (read_all(fd, &hdr, sizeof(hdr)) < 0) return -1;

    uint32_t magic = ntohl(hdr.magic);
    if (magic != IPC_MSG_MAGIC) {
        fprintf(stderr, "[IPC] Bad magic: %08x\n", magic);
        return -1;
    }

    meta->docid = ntohl(hdr.docid);
    meta->depth = ntohl(hdr.depth);

    if (meta->docid == IPC_SHUTDOWN_DOCID) return 0;

    uint16_t url_len  = ntohs(hdr.url_len);
    uint16_t path_len = ntohs(hdr.path_len);

    if (url_len  >= sizeof(meta->url))      url_len  = sizeof(meta->url)  - 1;
    if (path_len >= sizeof(meta->filepath)) path_len = sizeof(meta->filepath) - 1;

    if (read_all(fd, meta->url,      url_len)  < 0) return -1;
    if (read_all(fd, meta->filepath, path_len) < 0) return -1;
    meta->url[url_len - 1]           = '\0';
    meta->filepath[path_len - 1]     = '\0';
    return 1;
}

void ipc_server_close(int server_fd, int client_fd, const char *sock_path) {
    (void)server_fd;
    close(client_fd);
    unlink(sock_path);
}
