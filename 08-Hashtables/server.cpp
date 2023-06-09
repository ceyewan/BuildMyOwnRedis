#include "hashtable.h"
#include <arpa/inet.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <netinet/ip.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define container_of(ptr, type, member)                                        \
  ((type *)((char *)(ptr)-offsetof(type, member)))
#define LOG_INFO(x) fprintf(stderr, "[LOG_INFO] msg = %s\n", x);

const size_t k_max_msg = 4096;
const size_t k_max_args = 1024;

enum { STATE_REQ = 0, STATE_RES = 1, STATE_END = 2 };
enum { RES_OK = 0, RES_ERR = 1, RES_NX = 2 };

static struct {
  HMap db;
} g_data;

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
struct Entry {
  struct HNode node;
  std::string key;
  std::string val;
};

static bool entry_eq(HNode *lhs, HNode *rhs) {
  struct Entry *le = container_of(lhs, struct Entry, node);
  struct Entry *re = container_of(rhs, struct Entry, node);
  return lhs->hcode == rhs->hcode && le->key == re->key;
}

static uint64_t str_hash(const uint8_t *data, size_t len) {
  uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++) {
    h = (h + data[i]) * 0x01000193;
  }
  return h;
}

/* 通过 key 查找对应的值 */
static uint32_t do_get(std::vector<std::string> &cmd, uint8_t *res,
                       uint32_t *reslen) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return RES_NX;
  }
  std::string &val = container_of(node, Entry, node)->val;
  assert(val.size() <= k_max_msg);
  memcpy(res, val.data(), val.size());
  *reslen = (uint32_t)val.size();
  return RES_OK;
}

/* 重新设置键值对 */
static uint32_t do_set(std::vector<std::string> &cmd) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node) {
    container_of(node, Entry, node)->val.swap(cmd[2]);
  } else {
    Entry *ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->val.swap(cmd[2]);
    hm_insert(&g_data.db, &ent->node);
  }
  return RES_OK;
}

/* 删除元素 */
static uint32_t do_del(std::vector<std::string> &cmd) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
  if (node) {
    delete container_of(node, Entry, node);
  }
  return RES_OK;
}

/* 比较字符串是否相等 */
static bool cmd_is(std::string &str, const char *chr) {
  // return 0 == strcasecmp(str.c_str(), chr);
  return str == std::string(chr);
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
    LOG_INFO("fcntl error!");
    return;
  }
  flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);
  if (errno) {
    LOG_INFO("fcntl error!");
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
  /* EAGAIN 提示现在没有数据可读, 稍后重试 */
  if (ret < 0 && errno == EAGAIN) {
    return false;
  }
  if (ret < 0) {
    LOG_INFO("read() error");
    conn->state = STATE_END;
    return false;
  }
  /* EOF 表示文件终止符, 文件读完了 */
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
  /* 处理一个请求 */
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
    LOG_INFO("write error");
    conn->state = STATE_END;
    return false;
  }
  conn->wbuf_sent += (size_t)ret;
  assert(conn->wbuf_sent <= conn->wbuf_size);
  /* 数据全部写入了 fd 的缓冲区中*/
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
    LOG_INFO("accept() error!");
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
  conn_put(fd2conn, conn);
  return 0;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd > 0);
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  /* SO_REUSEADDR 允许在使用 bind 函数绑定端口之前
   * 重新使用处于 TIME_WAIT 状态的端口*/
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(0); // 0.0.0.0
  int ret = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
  assert(ret == 0);
  ret = listen(fd, SOMAXCONN);
  assert(ret == 0);
  fd_set_nb(fd);

  std::vector<Conn *> fd2conn;
  std::vector<struct pollfd> poll_args;

  while (true) {
    poll_args.clear();
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);

    for (Conn *conn : fd2conn) {
      if (!conn)
        continue;
      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }

    int ret = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if (ret < 0) {
      LOG_INFO("poll error!");
      return -1;
    }

    for (size_t i = 1; i < poll_args.size(); i++) {
      if (poll_args[i].revents) {
        Conn *conn = fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn->state == STATE_END) {
          fd2conn[conn->fd] = NULL;
          close(conn->fd);
          free(conn);
        }
      }
    }
    if (poll_args[0].revents) {
      accept_new_conn(fd2conn, fd);
    }
  }
  return 0;
}