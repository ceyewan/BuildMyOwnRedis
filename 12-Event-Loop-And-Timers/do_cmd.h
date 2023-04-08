#include <string>
#include <vector>

#define container_of(ptr, type, member)                                        \
  ((type *)((char *)(ptr)-offsetof(type, member)))

enum {
  SER_NIL = 0,
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_DBL = 4,
  SER_ARR = 5,
};
enum { T_STR = 0, T_ZSET = 1 };
enum { ERR_UNKNOWN = 1, ERR_2BIG = 2, ERR_TYPE = 3, ERR_ARG = 4 };

void out_err(std::string &out, int32_t code, const std::string &msg);
void do_get(std::vector<std::string> &cmd, std::string &out);
void do_set(std::vector<std::string> &cmd, std::string &out);
void do_del(std::vector<std::string> &cmd, std::string &out);
void do_keys(std::vector<std::string> &cmd, std::string &out);
void do_zadd(std::vector<std::string> &cmd, std::string &out);
void do_zquery(std::vector<std::string> &cmd, std::string &out);
void do_zrem(std::vector<std::string> &cmd, std::string &out);
void do_zscore(std::vector<std::string> &cmd, std::string &out);