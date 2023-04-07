#pragma once

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/ip.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define container_of(ptr, type, member)                                        \
  ((type *)((char *)(ptr)-offsetof(type, member)))

#define LOG_INFO(msg) printf("[LOG_INFO] %s\n", msg)
#define LOG_ERROR(msg)                                                         \
  {                                                                            \
    int err = errno;                                                           \
    printf("[LOG_ERROR] error = %d, msg = %s\n", err, msg);                    \
    abort();                                                                   \
  }

const size_t k_max_msg = 4096;
const size_t k_max_args = 1024;

enum {
  SER_NIL = 0,
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_DBL = 4,
  SER_ARR = 5,
};
enum { ERR_UNKNOWN = 1, ERR_2BIG = 2, ERR_TYPE = 3, ERR_ARG = 4 };
enum { STATE_REQ = 0, STATE_RES = 1, STATE_END = 2 };
enum { T_STR = 0, T_ZSET = 1 };

struct Conn {
  int fd = -1;
  uint32_t state = 0; // either STATE_REQ or STATE_RES
  // buffer for reading
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  // buffer for writing
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
};

bool try_one_request(Conn *conn);
void fd_set_nb(int fd);
int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd);
void connection_io(Conn *conn);

void state_res(Conn *conn);
void state_req(Conn *conn);