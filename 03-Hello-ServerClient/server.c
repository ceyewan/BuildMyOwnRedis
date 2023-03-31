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

void Handle(int fd);

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd > 0);
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  /* SO_REUSEADDR 允许在使用 bind 函数绑定端口之前
   * 重新使用处于 TIME_WAIT 状态的端口*/
  struct sockaddr_in addr;
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
    Handle(connfd);
    close(connfd);
  }
  close(fd);
  return 0;
}

void Handle(int fd) {
  char buf[64] = {};
  ssize_t size = read(fd, buf, sizeof(buf) - 1);
  if (size < 0) {
    LOG_ERROR("Read error!\n");
    return;
  }
  printf("client says: %s\n", buf);
  size = write(fd, "world", 5);
  if (size < 0) {
    LOG_ERROR("Write error!\n");
    return;
  }
}