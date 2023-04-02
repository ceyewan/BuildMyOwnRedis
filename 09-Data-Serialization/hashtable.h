#include <cstddef>
#include <cstdint>
#include <string>

#define container_of(ptr, type, member)                                        \
  ((type *)((char *)(ptr)-offsetof(type, member)))

struct HNode {
  HNode *next = NULL;
  uint64_t hcode = 0;
};

struct HTab {
  HNode **tab = NULL;
  size_t mask = 0;
  size_t size = 0;
};

struct HMap {
  HTab ht1;
  HTab ht2;
  size_t resizing_pos = 0;
};

struct Entry {
  struct HNode node;
  std::string key;
  std::string val;
};

const size_t k_resizing_work = 128;
const size_t k_max_load_factor = 8;

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_pop(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
size_t hm_size(HMap *hmap);
void hm_destroy(HMap *hmap);

void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg);

uint64_t str_hash(const uint8_t *data, size_t len);
bool entry_eq(HNode *lhs, HNode *rhs);