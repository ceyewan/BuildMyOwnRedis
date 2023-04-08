#include "common.h"

int main() {
  /* 创建 socket 套接字 */
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    LOG_ERROR("socket()");
  }
  /* SO_REUSEADDR 表示使用 bind 时可以重用处于 time_wait 状态的套接字 */
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  // bind
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0
  int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
  if (rv) {
    LOG_ERROR("bind()");
  }

  // listen
  rv = listen(fd, SOMAXCONN);
  if (rv) {
    LOG_ERROR("listen()");
  }
  // 非阻塞
  fd_set_nb(fd);

  // 所有的 Conn, fd2conn[fd] 为 fd 对于的 conn
  std::vector<Conn *> fd2conn;
  // the event loop
  std::vector<struct pollfd> poll_args;
  while (true) {
    // prepare the arguments of the poll()
    poll_args.clear();
    // for convenience, the listening fd is put in the first position
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);
    // connection fds
    for (Conn *conn : fd2conn) {
      if (!conn) {
        continue;
      }
      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }

    // poll for active fds
    // the timeout argument doesn't matter here
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if (rv < 0) {
      LOG_ERROR("poll");
    }
    // try to accept a new connection if the listening fd is active
    if (poll_args[0].revents) {
      (void)accept_new_conn(fd2conn, fd);
    }
    // process active connections
    for (size_t i = 1; i < poll_args.size(); ++i) {
      if (poll_args[i].revents) {
        Conn *conn = fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn->state == STATE_END) {
          // client closed normally, or something bad happened.
          // destroy this connection
          fd2conn[conn->fd] = NULL;
          (void)close(conn->fd);
          free(conn);
        }
      }
    }
  }
  return 0;
}
