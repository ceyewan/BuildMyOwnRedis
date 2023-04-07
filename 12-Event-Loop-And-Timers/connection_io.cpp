#include "common.h"

static bool try_fill_buffer(Conn *conn) {
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t rv = 0;
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv < 0 && errno == EINTR);
  /* EINTR 表示系统调用被信号中断, 需要重试
   * EAGAIN 表示操作会阻塞, 但是我们是非阻塞 IO, 那么不等了
   * 直接返回, 得到数据就绪时拿到 POLLIN 事件再来 */
  if (rv < 0 && errno == EAGAIN) {
    return false;
  }
  /* 返回值小于 0 说明出错了 */
  if (rv < 0) {
    LOG_INFO("read() error");
    conn->state = STATE_END;
    return false;
  }
  /* 没有读到数据, 说明读完了 */
  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      LOG_INFO("unexpected EOF");
    } else {
      LOG_INFO("EOF");
    }
    conn->state = STATE_END;
    return false;
  }

  conn->rbuf_size += (size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn->rbuf) - conn->rbuf_size);

  /* 为什么是一个循环呢? 因为这里是流水线操作, 一次可能读到多个 cmd */
  while (try_one_request(conn)) {
  }
  return (conn->state == STATE_REQ);
}

void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {
  }
}

static bool try_flush_buffer(Conn *conn) {
  ssize_t rv = 0;
  do {
    size_t remain = conn->wbuf_size - conn->wbuf_sent;
    rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
  } while (rv < 0 && errno == EINTR);

  if (rv < 0 && errno == EAGAIN) {
    return false;
  }
  if (rv < 0) {
    LOG_INFO("write() error");
    conn->state = STATE_END;
    return false;
  }
  conn->wbuf_sent += (size_t)rv;
  assert(conn->wbuf_sent <= conn->wbuf_size);
  if (conn->wbuf_sent == conn->wbuf_size) {
    conn->state = STATE_REQ;
    conn->wbuf_sent = 0;
    conn->wbuf_size = 0;
    return false;
  }
  /* 还没有 write 完成, 那么继续写 */
  return true;
}

void state_res(Conn *conn) {
  while (try_flush_buffer(conn)) {
  }
}

inline void connection_io(Conn *conn) {
  if (conn->state == STATE_REQ) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0);
  }
}