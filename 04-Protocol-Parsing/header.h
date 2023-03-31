#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/ip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOG_ERROR(x) printf("[LOG_ERROR]: %s\n", x);
#define LOG_INFO(x) printf("[LOG_INFO]: %s\n", x);

const size_t k_max_msg = 4096;

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t ret = read(fd, buf, n);
    LOG_INFO("read one time!");
    if (ret < 0) {
      LOG_ERROR("Read error!");
      return -1;
    }
    assert((size_t)ret <= n);
    n -= (size_t)ret;
    buf += ret;
  }
  return 0;
}

static int32_t write_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t ret = write(fd, buf, n);
    LOG_INFO("write one time!");
    if (ret < 0) {
      LOG_ERROR("Read error!");
      return -1;
    }
    assert((size_t)ret <= n);
    n -= (size_t)ret;
    buf += ret;
  }
  return 0;
}

int32_t query(int fd, const char *text);
int32_t handle(int fd);