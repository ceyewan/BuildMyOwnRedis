#include <assert.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
// proj
#include "common.h"

static bool entry_eq(HNode *lhs, HNode *rhs) {
  struct Entry *le = container_of(lhs, struct Entry, node);
  struct Entry *re = container_of(rhs, struct Entry, node);
  return lhs->hcode == rhs->hcode && le->key == re->key;
}

static void out_nil(std::string &out) { out.push_back(SER_NIL); }

static void out_str(std::string &out, const char *s, size_t size) {
  out.push_back(SER_STR);
  uint32_t len = (uint32_t)size;
  out.append((char *)&len, 4);
  out.append(s, len);
}

static void out_str(std::string &out, const std::string &val) {
  return out_str(out, val.data(), val.size());
}

static void out_int(std::string &out, int64_t val) {
  out.push_back(SER_INT);
  out.append((char *)&val, 8);
}

static void out_dbl(std::string &out, double val) {
  out.push_back(SER_DBL);
  out.append((char *)&val, 8);
}

void out_err(std::string &out, int32_t code, const std::string &LOG_INFO) {
  out.push_back(SER_ERR);
  out.append((char *)&code, 4);
  uint32_t len = (uint32_t)LOG_INFO.size();
  out.append((char *)&len, 4);
  out.append(LOG_INFO);
}

static void out_arr(std::string &out, uint32_t n) {
  out.push_back(SER_ARR);
  out.append((char *)&n, 4);
}

static void out_update_arr(std::string &out, uint32_t n) {
  assert(out[0] == SER_ARR);
  memcpy(&out[1], &n, 4);
}

/* 命令为 get key value, 在 哈希中查找, 如果没找到返回 nil,
 * 如果类型不为字符串, 那么结果出错, 直接返回 */
void do_get(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return out_nil(out);
  }
  Entry *ent = container_of(node, Entry, node);
  if (ent->type != T_STR) {
    return out_err(out, ERR_TYPE, "expect string type");
  }
  return out_str(out, ent->val);
}

/* 如果查询到了结果, 那么直接修改,
 * 否则, 创建一个新的 Entry 然后将该 Entry 插入哈希表中 */
void do_set(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node) {
    Entry *ent = container_of(node, Entry, node);
    if (ent->type != T_STR) {
      return out_err(out, ERR_TYPE, "expect string type");
    }
    ent->val.swap(cmd[2]);
  } else {
    Entry *ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->val.swap(cmd[2]);
    hm_insert(&g_data.db, &ent->node);
  }
  return out_nil(out);
}

static void entry_destroy(Entry *ent) {
  switch (ent->type) {
  case T_ZSET:
    zset_dispose(ent->zset);
    delete ent->zset;
    break;
  }
  delete ent;
}

static void entry_del_async(void *arg) { entry_destroy((Entry *)arg); }

static void entry_del(Entry *ent) {
  const size_t k_large_container_size = 10000;
  bool too_big = false;
  switch (ent->type) {
  case T_ZSET:
    too_big = hm_size(&ent->zset->hmap) > k_large_container_size;
    break;
  }
  if (too_big) {
    thread_pool_queue(&g_data.tp, &entry_del_async, ent);
  } else {
    entry_destroy(ent);
  }
}

/* 从哈希表中删除结果, 如果存在, 还需要删除 Entry */
void do_del(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
  if (node) {
    entry_del(container_of(node, Entry, node));
  }
  return out_int(out, node ? 1 : 0);
}

static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg) {
  if (tab->size == 0) {
    return;
  }
  for (size_t i = 0; i < tab->mask + 1; ++i) {
    HNode *node = tab->tab[i];
    while (node) {
      f(node, arg);
      node = node->next;
    }
  }
}

static void cb_scan(HNode *node, void *arg) {
  std::string &out = *(std::string *)arg;
  out_str(out, container_of(node, Entry, node)->key);
}

/* 输出 map 中所有的键, 遍历哈希表, 调用 cb 函数输出 */
void do_keys(std::vector<std::string> &cmd, std::string &out) {
  (void)cmd;
  out_arr(out, (uint32_t)hm_size(&g_data.db));
  h_scan(&g_data.db.ht1, &cb_scan, &out);
  h_scan(&g_data.db.ht2, &cb_scan, &out);
}

static bool str2dbl(const std::string &s, double &out) {
  char *endp = NULL;
  out = strtod(s.c_str(), &endp);
  return endp == s.c_str() + s.size() && !isnan(out);
}

static bool str2int(const std::string &s, int64_t &out) {
  char *endp = NULL;
  out = strtoll(s.c_str(), &endp, 10);
  return endp == s.c_str() + s.size();
}

/* zadd 命令: zadd zset 1.1 n1, 向有序集合中添加元素, zset 是有序集合的 key
 * 1.1 n1 是 (score, name) tuple 对, 添加到 key 对应的 zet 中 */
void do_zadd(std::vector<std::string> &cmd, std::string &out) {
  double score = 0;
  if (!str2dbl(cmd[2], score)) {
    return out_err(out, ERR_ARG, "expect fp number");
  }
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);
  Entry *ent = NULL;
  if (!hnode) {
    ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->type = T_ZSET;
    ent->zset = new ZSet();
    hm_insert(&g_data.db, &ent->node);
  } else {
    ent = container_of(hnode, Entry, node);
    if (ent->type != T_ZSET) {
      return out_err(out, ERR_TYPE, "expect zset");
    }
  }
  const std::string &name = cmd[3];
  bool added = zset_add(ent->zset, name.data(), name.size(), score);
  return out_int(out, (int64_t)added);
}

static bool expect_zset(std::string &out, std::string &s, Entry **ent) {
  Entry key;
  key.key.swap(s);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!hnode) {
    out_nil(out);
    return false;
  }
  *ent = container_of(hnode, Entry, node);
  if ((*ent)->type != T_ZSET) {
    out_err(out, ERR_TYPE, "expect zset");
    return false;
  }
  return true;
}

/* zrem zset name 查找到 zset 这个 Entry, 删除 name 对应的 tuple */
void do_zrem(std::vector<std::string> &cmd, std::string &out) {
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }
  const std::string &name = cmd[2];
  ZNode *znode = zset_pop(ent->zset, name.data(), name.size());
  if (znode) {
    znode_del(znode);
  }
  return out_int(out, znode ? 1 : 0);
}

/* zscore zset name 查找 zset 这个有序集合中 name 对于的 score */
void do_zscore(std::vector<std::string> &cmd, std::string &out) {
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }
  const std::string &name = cmd[2];
  ZNode *znode = zset_lookup(ent->zset, name.data(), name.size());
  return znode ? out_dbl(out, znode->score) : out_nil(out);
}

/* zquery zset score name offset limit 查询有序集合 zet 中 score name
 * 偏移 offset 输出 limit 个结果 */
void do_zquery(std::vector<std::string> &cmd, std::string &out) {
  double score = 0;
  if (!str2dbl(cmd[2], score)) {
    return out_err(out, ERR_ARG, "expect fp number");
  }
  const std::string &name = cmd[3];
  int64_t offset = 0;
  int64_t limit = 0;
  if (!str2int(cmd[4], offset)) {
    return out_err(out, ERR_ARG, "expect int");
  }
  if (!str2int(cmd[5], limit)) {
    return out_err(out, ERR_ARG, "expect int");
  }
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    if (out[0] == SER_NIL) {
      out.clear();
      out_arr(out, 0);
    }
    return;
  }
  if (limit <= 0) {
    return out_arr(out, 0);
  }
  ZNode *znode = zset_query(ent->zset, score, name.data(), name.size(), offset);
  out_arr(out, 0);
  uint32_t n = 0;
  while (znode && (int64_t)n < limit) {
    out_str(out, znode->name, znode->len);
    out_dbl(out, znode->score);
    znode = container_of(avl_offset(&znode->tree, +1), ZNode, tree);
    n += 2;
  }
  return out_update_arr(out, n);
}