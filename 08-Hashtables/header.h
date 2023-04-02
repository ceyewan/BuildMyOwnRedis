#include <arpa/inet.h>
#include <assert.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <netinet/ip.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define LOG_ERROR(x)                                                           \
  {                                                                            \
    int err = errno;                                                           \
    fprintf(stderr, "[LOG_ERROR] err = %d msg = %s\n", err, x);                \
    abort();                                                                   \
  }
#define LOG_INFO(x) fprintf(stderr, "[LOG_INFO] msg = %s\n", x);

const size_t k_max_msg = 4096;
const size_t k_max_args = 1024;

enum { STATE_REQ = 0, STATE_RES = 1, STATE_END = 2 };
enum { RES_OK = 0, RES_ERR = 1, RES_NX = 2 };

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

static std::map<std::string, std::string> g_map;

/* 解析请求 请求的格式为 nstr len1 str1 len2 str2 ... len_n, str_n */
static int32_t parse_req(const uint8_t *data, size_t len,
                         std::vector<std::string> &out) {
  if (len < 4) {
    return -1;
  }
  uint32_t nstr = 0;
  memcpy(&nstr, &data[0], 4);
  if (nstr > k_max_args) {
    return -1;
  }
  size_t pos = 4;
  while (nstr--) {
    if (pos + 4 > len) {
      return -1;
    }
    uint32_t sz = 0;
    memcpy(&sz, &data[pos], 4);
    if (pos + 4 + sz > len) {
      return -1;
    }
    out.push_back(std::string((char *)&data[pos + 4], sz));
    LOG_INFO(std::string((char *)&data[pos + 4], sz).data());
    pos += 4 + sz;
  }
  if (pos != len) {
    return -1;
  }
  return 0;
}

/* 通过 key 查找对应的值 */
static uint32_t do_get(const std::vector<std::string> &cmd, uint8_t *res,
                       uint32_t *reslen) {
  if (!g_map.count(cmd[1])) {
    return RES_NX;
  }
  std::string &val = g_map[cmd[1]];
  assert(val.size() <= k_max_msg);
  memcpy(res, val.data(), val.size());
  *reslen = (uint32_t)val.size();
  return RES_OK;
}

/* 重新设置键值对 */
static uint32_t do_set(const std::vector<std::string> &cmd) {
  g_map[cmd[1]] = cmd[2];
  return RES_OK;
}

/* 删除元素 */
static uint32_t do_del(const std::vector<std::string> &cmd) {
  g_map.erase(cmd[1]);
  return RES_OK;
}

/* 比较字符串是否相等 */
static bool cmd_is(std::string &str, const char *chr) {
  return 0 == strcasecmp(str.c_str(), chr);
}

/* 对请求进行操作 */
static int32_t do_request(const uint8_t *req, uint32_t reqlen,
                          uint32_t *rescode, uint8_t *res, uint32_t *reslen) {
  std::vector<std::string> cmd;
  if (0 != parse_req(req, reqlen, cmd)) {
    LOG_INFO("bad req");
    return -1;
  }
  if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    *rescode = do_get(cmd, res, reslen);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    *rescode = do_set(cmd);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
    *rescode = do_del(cmd);
  } else {
    *rescode = RES_ERR;
    const char *msg = "Unknown cmd";
    strcpy((char *)res, msg);
    *reslen = strlen(msg);
    return 0;
  }
  return 0;
}

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

/* 尝试处理 buffer 中的一个请求 */
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

  /* 得到一个 request, 并生成一个 response */
  uint32_t rescode = 0;
  uint32_t wlen = 0;
  int32_t err =
      do_request(&conn->rbuf[4], len, &rescode, &conn->wbuf[4 + 4], &wlen);
  if (err) {
    conn->state = STATE_END;
    return false;
  }
  wlen += 4;
  memcpy(&conn->wbuf[0], &wlen, 4);
  memcpy(&conn->wbuf[4], &rescode, 4);
  conn->wbuf_size = 4 + wlen;

  /* 移除 buffer 中的 request */
  size_t remain = conn->rbuf_size - 4 - len;
  if (remain) {
    memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  }
  conn->rbuf_size = remain;

  /* 改变 state, 处理 response */
  conn->state = STATE_RES;
  state_res(conn);
  return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn) {
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t ret = 0;
  /* 对于慢系统调用, 如这里的 read(), 指的是调用可能永远无法返回
   * 如果进程在一个系统调用中阻塞时, 当捕获到某个信号且信号相应处理函数返回时
   * 系统调用不再阻塞而是被中断, 返回错误(一般为 -1)并设置 errno = EINTR */
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    ret = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (ret < 0 && errno == EINTR);
  /* EAFAIN 提示现在没有数据可读, 稍后重试 */
  if (ret < 0 && errno == EAGAIN) {
    return false;
  }
  if (ret < 0) {
    LOG_ERROR("read() error");
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
  // 一个一个的处理请求
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

inline void connection_io(Conn *conn) {
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

inline int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    LOG_INFO("accept() error!");
    return -1;
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