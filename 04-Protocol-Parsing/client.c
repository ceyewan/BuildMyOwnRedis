#include "header.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd > 0);
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
  int ret = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
  assert(ret == 0);

  int32_t err = query(fd, "hello1");
  if (err) {
    goto L_DONE;
  }
  err = query(fd, "hello2");
  if (err) {
    goto L_DONE;
  }
  err = query(fd, "hello3");
  if (err) {
    goto L_DONE;
  }
L_DONE:
  close(fd);
  return 0;
}

int32_t query(int fd, const char *text) {
  uint32_t len = (uint32_t)strlen(text);
  if (len > k_max_msg) {
    return -1;
  }
  char wbuf[4 + k_max_msg];
  memcpy(wbuf, &len, 4); // assume little endian
  memcpy(&wbuf[4], text, len);
  int err = write_full(fd, wbuf, 4 + len);
  if (err)
    return err;

  char rbuf[4 + k_max_msg + 1];
  errno = 0;
  err = read_full(fd, rbuf, 4);
  if (err) {
    if (errno == 0) {
      LOG_ERROR("EOF");
    } else {
      LOG_ERROR("Read error!");
    }
  }

  memcpy(&len, rbuf, 4);
  if (len > k_max_msg) {
    LOG_ERROR("Too long!");
    return -1;
  }

  err = read_full(fd, &rbuf[4], len);
  if (err) {
    LOG_ERROR("Read error!");
    return err;
  }

  rbuf[4 + len] = '\0';
  printf("server says: %s\n", &rbuf[4]);
  return 0;
}