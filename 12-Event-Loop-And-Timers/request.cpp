#include "common.h"
#include "do_map.h"
#include "do_out.h"
#include "do_zset.h"

/* 解析请求, 命令格式为 格式为 nstr len str len str ..., 循环 nstr 次 */
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
    pos += 4 + sz;
  }

  if (pos != len) {
    return -1;
  }
  return 0;
}

static bool cmd_is(const std::string &word, const char *cmd) {
  return 0 == strcasecmp(word.c_str(), cmd);
}

/* keys 请求, 将 map 中的数据全部返回
 * get 请求, 如 get k, 从 map 中拿到 v 返回
 * set 请求, 如 set k v, 在 map 中添加 kv 对
 * del 请求, 如 del k, 从 map 中删除 k
 * zadd 请求, ru zadd k v, 向 zset 中添加 kv 对
 * TO DO */
static void do_request(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() == 1 && cmd_is(cmd[0], "keys")) {
    do_keys(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    do_get(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    do_set(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
    do_del(cmd, out);
  } else if (cmd.size() == 4 && cmd_is(cmd[0], "zadd")) {
    do_zadd(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "zrem")) {
    do_zrem(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "zscore")) {
    do_zscore(cmd, out);
  } else if (cmd.size() == 6 && cmd_is(cmd[0], "zquery")) {
    do_zquery(cmd, out);
  } else {
    out_err(out, ERR_UNKNOWN, "Unknown cmd");
  }
}

bool try_one_request(Conn *conn) {
  /* 请求需要有一个 len, 字节长度为 4, 后续消息的长度为 len */
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
  assert(4 + len <= conn->rbuf_size);

  /* 解析命令, 格式为 nstr len str len str ... */
  std::vector<std::string> cmd;
  if (0 != parse_req(&conn->rbuf[4], len, cmd)) {
    LOG_INFO("bad req");
    conn->state = STATE_END;
    return false;
  }

  /* 通过请求, 得到响应 */
  std::string out;
  do_request(cmd, out);

  if (4 + out.size() > k_max_msg) {
    out.clear();
    out_err(out, ERR_2BIG, "response is too big");
  }

  /* 将响应写入 wbuf */
  uint32_t wlen = (uint32_t)out.size();
  memcpy(&conn->wbuf[0], &wlen, 4);
  memcpy(&conn->wbuf[4], out.data(), out.size());
  conn->wbuf_size = 4 + wlen;

  /* 将已经处理的请求从 rbuf 中移除, 始终保持 rbuf 中有足够的空间
   * 每个请求都需要 move 一次, 性能开销太大, 使用 readv, 判断是否需要 move */
  size_t remain = conn->rbuf_size - 4 - len;
  if (remain) {
    memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  }
  conn->rbuf_size = remain;

  /* 调用 state_res() 处理响应 */
  conn->state = STATE_RES;
  state_res(conn);
  // 如果还是 STATE_REQ 请求, 那么就会继续 try_one_request
  return (conn->state == STATE_REQ);
}