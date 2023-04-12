#pragma once

#include "hashtable.h"
#include "thread.h"
#include "zset.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
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

inline uint64_t str_hash(const uint8_t *data, size_t len) {
  uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++) {
    h = (h + data[i]) * 0x01000193;
  }
  return h;
}

/* list.h */
struct DList {
  DList *prev = NULL;
  DList *next = NULL;
};

/* server.cpp */
struct Conn {
  int fd = -1;
  uint32_t state = 0; // either STATE_REQ or STATE_RES
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
  uint64_t idle_start = 0;
  DList idle_list;
};

struct Entry {
  struct HNode node;
  std::string key;
  std::string val;
  uint32_t type = 0;
  ZSet *zset = NULL;
};

static struct {
  HMap db;
  std::vector<Conn *> fd2conn;
  DList idle_list;
  ThreadPool tp;
} g_data;

enum { T_STR = 0, T_ZSET = 1 };
enum { STATE_REQ = 0, STATE_RES = 1, STATE_END = 2 };
enum { ERR_UNKNOWN = 1, ERR_2BIG = 2, ERR_TYPE = 3, ERR_ARG = 4 };
enum {
  SER_NIL = 0,
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_DBL = 4,
  SER_ARR = 5
};

/* do_connect.cpp */
int init_socket();
void fd_set_nb(int);
int32_t accept_new_conn(int);
void conn_done(Conn *);

/* do_io.cpp */
void connection_io(Conn *);
void state_res(Conn *);

/* do_request.cpp */
bool try_one_request(Conn *);

/* do_work.cpp */
void do_keys(std::vector<std::string> &, std::string &);
void do_get(std::vector<std::string> &, std::string &);
void do_set(std::vector<std::string> &, std::string &);
void do_del(std::vector<std::string> &, std::string &);
void do_zadd(std::vector<std::string> &, std::string &);
void do_zrem(std::vector<std::string> &, std::string &);
void do_zscore(std::vector<std::string> &, std::string &);
void do_zquery(std::vector<std::string> &, std::string &);
void out_err(std::string &, int32_t, const std::string &);