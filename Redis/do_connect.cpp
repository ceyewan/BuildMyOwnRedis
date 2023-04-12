
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
// proj
#include "common.h"
#include "list.h"

int init_socket() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    LOG_ERROR("socket()");
  }
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(8888);
  addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0
  int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
  if (rv) {
    LOG_ERROR("bind()");
  }
  rv = listen(fd, SOMAXCONN);
  if (rv) {
    LOG_ERROR("listen()");
  }
  return fd;
}

void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    LOG_ERROR("fcntl error");
    return;
  }
  flags |= O_NONBLOCK;
  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);
  if (errno) {
    LOG_ERROR("fcntl error");
  }
}

static void conn_put(struct Conn *conn) {
  if (g_data.fd2conn.size() <= (size_t)conn->fd) {
    g_data.fd2conn.resize(conn->fd + 1);
  }
  g_data.fd2conn[conn->fd] = conn;
}

int32_t accept_new_conn(int fd) {
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    LOG_INFO("accept() error");
    return -1;
  }
  fd_set_nb(connfd);
  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn) {
    close(connfd);
    return -1;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  conn->idle_start = get_monotonic_usec();
  printf("start insert!\n");
  dlist_init(&conn->idle_list);
  dlist_insert_before(&g_data.idle_list, &conn->idle_list);
  printf("finish insert!\n");
  conn_put(conn);
  return 0;
}

void conn_done(Conn *conn) {
  g_data.fd2conn[conn->fd] = NULL;
  (void)close(conn->fd);
  dlist_detach(&conn->idle_list);
  free(conn);
}