#include "ipc_client.h"
#include "../common/ipc_proto.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

int ipc_connect(const char *sock_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    return fd;
}

static int write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w <= 0) return -1;
        p += w; n -= (size_t)w;
    }
    return 0;
}

int ipc_send_doc(int fd, unsigned int docid, const char *url,
                 const char *filepath, unsigned int depth) {
    uint16_t url_len  = (uint16_t)(strlen(url)      + 1);
    uint16_t path_len = (uint16_t)(strlen(filepath) + 1);

    ipc_msg_hdr_t hdr;
    hdr.magic    = htonl(IPC_MSG_MAGIC);
    hdr.docid    = htonl(docid);
    hdr.depth    = htonl(depth);
    hdr.url_len  = htons(url_len);
    hdr.path_len = htons(path_len);

    if (write_all(fd, &hdr, sizeof(hdr))  < 0) return -1;
    if (write_all(fd, url,  url_len)      < 0) return -1;
    if (write_all(fd, filepath, path_len) < 0) return -1;
    return 0;
}

int ipc_send_shutdown(int fd) {
    ipc_msg_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = htonl(IPC_MSG_MAGIC);
    hdr.docid = htonl(IPC_SHUTDOWN_DOCID);
    return write_all(fd, &hdr, sizeof(hdr));
}

void ipc_close(int fd) {
    close(fd);
}
