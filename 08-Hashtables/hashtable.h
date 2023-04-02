#include <cassert>
#include <cstddef>
#include <cstdlib>

struct HNode {
  HNode *next = NULL;
  uint64_t hcode = 0;
};

struct HTab {
  HNode **tab = NULL;
  size_t mask = 0;
  size_t size = 0;
};

static void h_init(HTab *htab, size_t n) {
  assert(n > 0 && ((n - 1) & n) == 0);
  htab->tab = (HNode **)calloc(sizeof(struct HNode *), n);
  htab->mask = n - 1;
  htab->size = 0;
}

static void h_insert(HTab *htab, HNode *node) {
  size_t pos = node->hcode & htab->mask;
  HNode *next = htab->tab[pos];
  node->next = next;
  htab->tab[pos] = node;
  htab->size++;
}

static HNode **h_lookup(HTab *htab, HNode *key, bool (*cmp)(HNode *, HNode *)) {
  if (!htab->tab)
    return NULL;
  size_t pos = key->hcode & htab->mask;
  HNode **from = &htab->tab[pos];
  while (*from) {
    if (cmp(*from, key)) {
      return from;
    }
    from = &(*from)->next;
  }
  return NULL;
}

static HNode *h_detach(HTab *htab, HNode **from) {
  HNode *node = *from;
  *from = (*from)->next;
  htab->size--;
  return node;
}

struct HMap {
  HTab ht1;
  HTab ht2;
  size_t resizing_pos = 0;
};

const size_t k_resizing_work = 128;

static void hm_help_resizing(HMap *hmap) {
  if (hmap->ht2.tab == NULL)
    return;
  size_t nwork = 0;
  while (nwork < k_resizing_work && hmap->ht2.size > 0) {
    HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
    if (!*from) {
      hmap->resizing_pos++;
      continue;
    }
    h_insert(&hmap->ht1, h_detach(&hmap->ht2, from));
    nwork++;
  }
  if (hmap->ht2.size == 0) {
    free(hmap->ht2.tab);
    hmap->ht2 = HTab{};
  }
}
static HNode *hm_lookup(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *)) {
  hm_help_resizing(hmap);
  HNode **from = h_lookup(&hmap->ht1, key, cmp);
  if (!from) {
    from = h_lookup(&hmap->ht2, key, cmp);
  }
  return from ? *from : NULL;
}

const size_t k_max_load_factor = 8;

static void hm_start_resizing(HMap *hmap) {
  assert(hmap->ht2.tab == NULL);
  // create a bigger hashtable and swap them
  hmap->ht2 = hmap->ht1;
  h_init(&hmap->ht1, (hmap->ht1.mask + 1) * 2);
  hmap->resizing_pos = 0;
}

static void hm_insert(HMap *hmap, HNode *node) {
  if (!hmap->ht1.tab) {
    h_init(&hmap->ht1, 4);
  }
  h_insert(&hmap->ht1, node);
  if (!hmap->ht2.tab) {
    size_t load_factor = hmap->ht1.size / (hmap->ht1.mask + 1);
    if (load_factor >= k_max_load_factor) {
      hm_start_resizing(hmap);
    }
  }
  hm_help_resizing(hmap);
}

static HNode *hm_pop(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *)) {
  hm_help_resizing(hmap);
  HNode **from = h_lookup(&hmap->ht1, key, cmp);
  if (from) {
    return h_detach(&hmap->ht1, from);
  }
  from = h_lookup(&hmap->ht2, key, cmp);
  if (from) {
    return h_detach(&hmap->ht2, from);
  }
  return NULL;
}