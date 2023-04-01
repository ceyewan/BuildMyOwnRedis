#include <arpa/inet.h>
#include <assert.h>
#include <cassert>
#include <cstddef>
#include <errno.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define LOG_ERROR(x) printf("[LOG_ERROR]: %s\n", x);
#define LOG_INFO(x) printf("[LOG_INFO]: %s\n", x);

const size_t k_max_msg = 4096;

enum { STATE_REQ = 0, STATE_RES = 1, STATE_END = 2 };

struct Conn {
  int fd = -1;
  uint32_t state = 0;
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
};

static void state_req(Conn *conn);
static void state_res(Conn *conn);

static void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    LOG_ERROR("fcntl error!");
    return;
  }
  flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);
  if (errno) {
    LOG_ERROR("fcntl error!");
    return;
  }
}

static bool try_one_request(Conn *conn) {
  if (conn->rbuf_size < 4) {
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, &conn->rbuf[0], 4);
  if (len > k_max_msg) {
    LOG_INFO("too long");
    conn->state = STATE_END;
    return false;
  }
  if (4 + len > conn->rbuf_size) {
    return false;
  }

  printf("client says: %.*s\n", len, &conn->rbuf[4]);

  memcpy(&conn->wbuf[0], &len, 4);
  memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
  conn->wbuf_size = 4 + len;

  size_t remain = conn->rbuf_size - 4 - len;
  if (remain) {
    memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  }
  conn->rbuf_size = remain;

  conn->state = STATE_RES;
  state_res(conn);
  return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn) {
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t ret = 0;
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    ret = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (ret < 0 && errno == EINTR);
  if (ret < 0 && errno == EAGAIN) {
    return false;
  }
  if (ret < 0) {
    LOG_ERROR("read error");
    conn->state = STATE_END;
    return false;
  }
  if (ret == 0) {
    if (conn->rbuf_size > 0) {
      LOG_INFO("unexpected EOF");
    } else {
      LOG_INFO("EOF");
    }
    conn->state = STATE_END;
    return false;
  }
  conn->rbuf_size += (size_t)ret;
  assert(conn->rbuf_size <= sizeof(conn->rbuf) - conn->rbuf_size);
  while (try_one_request(conn)) {
  }
  return conn->state == STATE_REQ;
}

static void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {
  }
}

static bool try_flush_buffer(Conn *conn) {
  ssize_t ret = 0;
  do {
    size_t remain = conn->wbuf_size - conn->wbuf_sent;
    ret = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
  } while (ret < 0 && errno == EINTR);
  if (ret < 0 && errno == EAGAIN) {
    return false;
  }
  if (ret < 0) {
    LOG_ERROR("write error");
    conn->state = STATE_END;
    return false;
  }
  conn->wbuf_sent += (size_t)ret;
  assert(conn->wbuf_sent <= conn->wbuf_size);

  if (conn->wbuf_sent == conn->wbuf_size) {
    conn->state = STATE_REQ;
    conn->wbuf_sent = 0;
    conn->wbuf_size = 0;
    return false;
  }
  return true;
}

static void state_res(Conn *conn) {
  while (try_flush_buffer(conn)) {
  }
}

static void connection_io(Conn *conn) {
  if (conn->state == STATE_REQ) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0);
  }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    LOG_INFO("accept() error");
    return -1; // error
  }
  fd_set_nb(connfd);
  struct Conn *conn = new (struct Conn);
  if (!conn) {
    close(connfd);
    return -1;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  conn_put(fd2conn, conn);
  return 0;
}