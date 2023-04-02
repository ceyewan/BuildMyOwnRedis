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

#define LOG_INFO(x) fprintf(stderr, "[LOG_INFO] msg = %s\n", x);

const size_t k_max_msg = 4096;

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv <= 0) {
      return -1;
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = write(fd, buf, n);
    if (rv <= 0) {
      return -1;
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
  // 消息的总长度(nstr + len_i + str_i)
  uint32_t len = 4;
  for (const std::string &s : cmd) {
    len += 4 + s.size();
  }
  if (len > k_max_msg) {
    return -1;
  }
  // 写入消息总长度
  char wbuf[4 + k_max_msg];
  memcpy(wbuf, &len, 4); // assume little endian
  // 写入 nstr
  uint32_t n = cmd.size();
  memcpy(&wbuf[4], &n, 4);
  // 循环写入 len_i 和 str_i
  size_t cur = 8;
  for (const std::string &s : cmd) {
    uint32_t p = (uint32_t)s.size();
    memcpy(&wbuf[cur], &p, 4);
    memcpy(&wbuf[cur + 4], s.data(), s.size());
    cur += 4 + s.size();
  }
  return write_all(fd, wbuf, 4 + len);
}

static int32_t read_res(int fd) {
  char rbuf[4 + k_max_msg + 1];
  // 读出 len, 表示消息的长度
  errno = 0;
  int32_t err = read_full(fd, rbuf, 4);
  if (err) {
    if (errno == 0) {
      LOG_INFO("EOF");
    } else {
      LOG_INFO("read() error");
    }
    return err;
  }
  uint32_t len = 0;
  memcpy(&len, rbuf, 4); // assume little endian
  if (len > k_max_msg) {
    LOG_INFO("too long");
    return -1;
  }
  // 读出回应的消息体
  err = read_full(fd, &rbuf[4], len);
  if (err) {
    LOG_INFO("read() error");
    return err;
  }
  uint32_t rescode = 0;
  if (len < 4) {
    LOG_INFO("bad response");
    return -1;
  }
  // 读出 rescode, 然后将消息打印出来
  memcpy(&rescode, &rbuf[4], 4);
  printf("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]);
  return 0;
}

int main(int argc, char **argv) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // 127.0.0.1
  int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv) {
    return -1;
  }

  std::vector<std::string> cmd;
  for (int i = 1; i < argc; i++) {
    cmd.push_back(argv[i]);
  }
  int32_t err = send_req(fd, cmd);
  if (err)
    goto L_DONE;
  err = read_res(fd);
  if (err)
    goto L_DONE;

L_DONE:
  close(fd);
  return 0;
}