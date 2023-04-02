#include "hashtable.h"
#include <arpa/inet.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <netinet/ip.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define LOG_INFO(x) fprintf(stderr, "[LOG_INFO] msg = %s\n", x);

const size_t k_max_msg = 4096;
const size_t k_max_args = 1024;

enum { STATE_REQ = 0, STATE_RES = 1, STATE_END = 2 };
enum { RES_OK = 0, RES_ERR = 1, RES_NX = 2 };
enum { ERR_UNKNOWN = 1, ERR_2BIG = 2 };
enum { SER_NIL = 0, SER_ERR = 1, SER_STR = 2, SER_INT = 3, SER_ARR = 4 };

static struct {
  HMap db;
} g_data;

struct Conn {
  int fd = -1;
  uint32_t state = 0;
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
};

void state_req(Conn *conn);
void state_res(Conn *conn);

int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd);
void fd_set_nb(int fd);
void connection_io(Conn *conn);