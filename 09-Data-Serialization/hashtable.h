#include <cstddef>
#include <cstdint>

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

const size_t k_resizing_work = 128;
const size_t k_max_load_factor = 8;

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_pop(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
size_t hm_size(HMap *hmap);
void hm_destroy(HMap *hmap);