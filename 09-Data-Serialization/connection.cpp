#include "header.h"

void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
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

void fd_set_nb(int fd) {
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

static void out_nil(std::string &out) { out.push_back(SER_NIL); }

static void out_str(std::string &out, const std::string &val) {
  out.push_back(SER_STR);
  uint32_t len = (uint32_t)val.size();
  out.append((char *)&len, 4);
  out.append(val);
}

static void out_int(std::string &out, int64_t val) {
  out.push_back(SER_INT);
  out.append((char *)&val, 8);
}

static void out_err(std::string &out, int32_t code, const std::string &msg) {
  out.push_back(SER_ERR);
  out.append((char *)&code, 4);
  uint32_t len = (uint32_t)msg.size();
  out.append((char *)&len, 4);
  out.append(msg);
}

static void out_arr(std::string &out, uint32_t n) {
  out.push_back(SER_ARR);
  out.append((char *)&n, 4);
}

/* 通过 key 查找对应的值 */
static void do_get(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return out_nil(out);
  }
  std::string &val = container_of(node, Entry, node)->val;
  assert(val.size() <= k_max_msg);
  out_str(out, val);
}

/* 重新设置键值对 */
static void do_set(std::vector<std::string> &cmd, std::string &out) {
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
  return out_nil(out);
}

/* 删除元素 */
static void do_del(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
  if (node) {
    delete container_of(node, Entry, node);
  }
  return out_int(out, node ? 1 : 0);
}

static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg) {
  if (tab->size == 0) {
    return;
  }
  for (size_t i = 0; i < tab->mask + 1; i++) {
    HNode *node = tab->tab[i];
    while (node) {
      f(node, arg);
      node = node->next;
    }
  }
}

static void cb_scan(HNode *node, void *arg) {
  std::string &out = *(std::string *)arg;
  out_str(out, container_of(node, Entry, node)->key);
}

static void do_keys(std::vector<std::string> &cmd, std::string &out) {
  (void)cmd;
  out_arr(out, (uint32_t)hm_size(&g_data.db));
  h_scan(&g_data.db.ht1, &cb_scan, &out);
  h_scan(&g_data.db.ht2, &cb_scan, &out);
}

/* 比较字符串是否相等 */
static bool cmd_is(std::string &str, const char *chr) {
  // return 0 == strcasecmp(str.c_str(), chr);
  return str == std::string(chr);
}

/* 对请求进行操作 */
static void do_request(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() == 1 && cmd_is(cmd[0], "keys")) {
    do_keys(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    do_get(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    do_set(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
    do_del(cmd, out);
  } else {
    out_err(out, ERR_UNKNOWN, "Unknown cmd");
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
  std::vector<std::string> cmd;
  if (0 != parse_req(&conn->rbuf[4], len, cmd)) {
    LOG_INFO("bad req");
    conn->state = STATE_END;
    return false;
  }
  std::string out;
  do_request(cmd, out);

  if (4 + out.size() > k_max_msg) {
    out.clear();
    out_err(out, ERR_2BIG, "response is too big");
  }
  uint32_t wlen = (uint32_t)out.size();
  memcpy(&conn->wbuf[0], &wlen, 4);
  memcpy(&conn->wbuf[4], out.data(), out.size());
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

void state_req(Conn *conn) {
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

void state_res(Conn *conn) {
  while (try_flush_buffer(conn)) {
  }
}

void connection_io(Conn *conn) {
  if (conn->state == STATE_REQ) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0);
  }
}