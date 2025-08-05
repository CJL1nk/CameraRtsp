#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "utils/Platform.h"

#define SOCKET_ADDR_LEN INET_ADDRSTRLEN

typedef sockaddr_in s_addr_t;
typedef socklen_t s_addrlen_t;

typedef struct {
    s_addr_t address;
    s_addrlen_t addrlen;
    fd_set read_fds;
    int_t pipe_fd[2];
    int_t socket;
} CancellableSocket;

// Socket functions
static inline int_t InitServer(
    CancellableSocket& sock,
    int_t port,
    int_t backlog) {

    int_t fd = socket(AF_INET, SOCK_STREAM, 0);
    int_t opt = 1;

    if (fd < 0) {
        return -1;
    }

    if (setsockopt(fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) < 0) {
        close(fd);
        return -2;
    }

    Reset(&sock.address, sock.addrlen);
    sock.address.sin_family = AF_INET;
    sock.address.sin_addr.s_addr = INADDR_ANY;
    sock.address.sin_port = htons(port);
    sock.addrlen = sizeof(sock.address);

    if (bind(fd, (struct sockaddr*)&sock.address, sock.addrlen) < 0) {
        close(fd);
        return -3;
    }

    if (listen(fd, backlog) < 0) {
        close(fd);
        return -4;
    }

    if (pipe(sock.pipe_fd) < 0) {
        close(fd);
        return -5;
    }

    sock.socket = fd;
    return fd;
}

static inline bool_t IsConnected(const CancellableSocket& socket) {
    return socket.socket >= 0;
}

// Some magic I copy from internet
static inline int_t Wait(CancellableSocket& socket) {
    FD_ZERO(&socket.read_fds);
    FD_SET(socket.socket, &socket.read_fds);
    FD_SET(socket.pipe_fd[0], &socket.read_fds);

    int_t max_fd = socket.socket > socket.pipe_fd[0] ?
            socket.socket : socket.pipe_fd[0];
    select(max_fd + 1, &socket.read_fds, nullptr, nullptr, nullptr);

    if (FD_ISSET(socket.pipe_fd[0], &socket.read_fds)) {
        char_t buffer[1]; // Flush
        read(socket.pipe_fd[0], buffer, 1);
        return -1;
    }

    if (FD_ISSET(socket.socket, &socket.read_fds)) {
        return 0;
    }
    return -1;
}

static inline int_t Accept(CancellableSocket& client, const CancellableSocket& server_socket) {
    client.addrlen = sizeof(client.address);
    int_t client_socket = accept(server_socket.socket,
                                 (sockaddr *) &client.address,
                                 &client.addrlen);
    if (client_socket < 0) {
        return -1;
    }

    if (pipe(client.pipe_fd) < 0) {
        close(client_socket);
        return -2;
    }

    client.socket = client_socket;
    return 0;
}

static inline const char_t *GetSocketAddr(
        const CancellableSocket& socket,
        char_t *dst,
        sz_t size) {
    return inet_ntop(AF_INET,
                     &socket.address.sin_addr,
                     dst,
                     (socklen_t)size);
}

static inline ssz_t Send(
        const CancellableSocket& socket,
        const void *buf,
        sz_t len,
        int_t flags) {
    return send(socket.socket, buf, len, flags);
}

static inline ssz_t Receive(
        const CancellableSocket& socket,
        void *buf,
        sz_t len,
        int_t flags) {
    return recv(socket.socket, buf, len, flags);
}

static inline void Interrupt(CancellableSocket& socket) {
    WriteFile(socket.pipe_fd[1], "x", 1);
}

static inline void Destroy(CancellableSocket& socket) {
    close(socket.socket);
    close(socket.pipe_fd[0]);
    close(socket.pipe_fd[1]);
    socket.socket = -1;
}