#include "header.h"

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
  while (1) {
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
      LOG_ERROR("Accept return connfd < 0");
      continue;
    }
    while (1) {
      int32_t err = handle(connfd);
      if (err)
        break;
    }
    close(connfd);
  }
  close(fd);
  return 0;
}

int32_t handle(int fd) {
  char rbuf[4 + k_max_msg + 1];
  errno = 0;
  int32_t err = read_full(fd, rbuf, 4);
  if (err) {
    if (errno == 0) {

    } else {
    }
    return err;
  }
  uint32_t len = 0;
  memcpy(&len, rbuf, 4);
  if (len > k_max_msg) {
    LOG_ERROR("too long");
    return -1;
  }
  err = read_full(fd, &rbuf[4], len);
  if (err) {
    LOG_ERROR("Read error!");
    return err;
  }

  rbuf[4 + len] = '\0';
  printf("client says: %s\n", &rbuf[4]);

  const char reply[] = "world";
  char wbuf[4 + sizeof(reply)] = {};
  len = (uint32_t)strlen(reply);
  memcpy(wbuf, &len, 4);
  memcpy(&wbuf[4], reply, len);
  return write_full(fd, wbuf, 4 + len);
}