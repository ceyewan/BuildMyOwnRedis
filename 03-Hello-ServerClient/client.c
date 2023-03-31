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

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd > 0);
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
  int ret = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
  assert(ret == 0);
  ssize_t size = write(fd, "hello", 5);
  char buf[64] = {};
  size = read(fd, buf, sizeof(buf));
  if (size < 0) {
    LOG_ERROR("Read error!");
    return 0;
  }
  printf("server says: %s\n", buf);
  close(fd);
  return 0;
}