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
      LOG_ERROR("poll error!");
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