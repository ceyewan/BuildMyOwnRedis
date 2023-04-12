#include "common.h"
#include "list.h"
#include "thread.h"
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>

std::vector<struct pollfd> poll_args;

void init_poll(int fd) {
  poll_args.clear();
  struct pollfd pfd = {fd, POLLIN, 0};
  poll_args.push_back(pfd);
  for (Conn *conn : g_data.fd2conn) {
    if (!conn) {
      continue;
    }
    struct pollfd pfd = {};
    pfd.fd = conn->fd;
    pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
    pfd.events = pfd.events | POLLERR;
    poll_args.push_back(pfd);
  }
}

int main() {
  int fd = init_socket();
  fd_set_nb(fd);
  dlist_init(&g_data.idle_list);
  thread_pool_init(&g_data.tp, 4);
  while (true) {
    init_poll(fd);
    int timeout_ms = (int)next_timer_ms();
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), timeout_ms);
    if (rv < 0) {
      LOG_ERROR("poll");
    }
    if (poll_args[0].revents) {
      accept_new_conn(fd);
    }
    for (size_t i = 1; i < poll_args.size(); ++i) {
      if (poll_args[i].revents) {
        Conn *conn = g_data.fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn->state == STATE_END) {
          conn_done(conn);
        }
      }
    }
    process_timers(conn_done);
  }
  return 0;
}
