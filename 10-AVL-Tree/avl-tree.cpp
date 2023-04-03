#include <cstddef>
#include <cstdint>

struct AVLNode {
  uint32_t depth = 0;
  uint32_t cnt = 0;
  AVLNode *left = NULL;
  AVLNode *right = NULL;
  AVLNode *parent = NULL;
};

static void avl_init(AVLNode *node) {
  node->depth = 1;
  node->cnt = 1;
  node->left = node->right = node->parent = NULL;
}

static uint32_t avl_depth(AVLNode *node) { return node ? node->depth : 0; }

static uint32_t avl_cnt(AVLNode *node) { return node ? node->cnt : 0; }

static uint32_t max(uint32_t lhs, uint32_t rhs) {
  return lhs < rhs ? rhs : lhs;
}

static void avl_update(AVLNode *node) {
  node->depth = 1 + max(avl_depth(node->left), avl_depth(node->right));
  node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

/* 左旋操作, 将右子树 new_node 设为根节点
 * 1. 将 new_node 的左子树设为 node 的右子树
 * 2. 将 node 设为 new_node 的左子树
 * 3. 调整 node 和 new_node 的深度以及大小 */
static AVLNode *root_left(AVLNode *node) {
  AVLNode *new_node = node->right; // 右子树
  // 操作 1
  if (new_node->left) {
    new_node->left->parent = node;
  }
  node->right = new_node->left;
  // 操作 2
  new_node->left = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  // 操作 3
  avl_update(node);
  avl_update(new_node);
  return new_node;
}

/* 右旋操作, 将左子树 new_node 设为根节点
 * 1. 将 new_node 的右子树设为 node 的左子树
 * 2. 将 node 设为 new_node 的右子树
 * 3. 调整 node 和 new_node 的深度以及大小 */
static AVLNode *root_right(AVLNode *node) {
  AVLNode *new_node = node->left;
  // 操作 1
  if (new_node->right) {
    new_node->right->parent = node;
  }
  node->left = new_node->right;
  // 操作 2
  new_node->right = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  // 操作 3
  avl_update(node);
  avl_update(new_node);
  return new_node;
}

/* 左子树高, 需要调用右旋
 * 考虑当左子树右子树高时, 也就是 LR 型, 需要先对左子树左旋转化为 LL 型 */
static AVLNode *avl_fix_left(AVLNode *root) {
  if (avl_depth(root->left->left) < avl_depth(root->left->right)) {
    root->left = root_left(root->left);
  }
  return root_right(root);
}

/* 右子树高, 需要调用左旋
 * 考虑当右子树左子树高时, 也就是 RL 型, 需要先对右子树右旋转化为 RR 型 */
static AVLNode *avl_fix_right(AVLNode *root) {
  if (avl_depth(root->right->right) < avl_depth(root->right->left)) {
    root->right = root_right(root->right);
  }
  return root_left(root);
}

static AVLNode *avl_fix(AVLNode *node) {
  while (true) {
    avl_update(node);
    uint32_t l = avl_depth(node->left);
    uint32_t r = avl_depth(node->right);
    AVLNode **from = NULL;
    // from 为 node->parent->left/right 的地址, 在子树调整好之后
    // 需要将 *from 设置子树的 root 节点
    if (node->parent) {
      from = (node->parent->left == node) ? &node->parent->left
                                          : &node->parent->right;
    }
    if (l == r + 2) {
      node = avl_fix_left(node);
    } else if (l + 2 == r) {
      node = avl_fix_right(node);
    }
    if (!from) {
      return node;
    }
    *from = node;
    node = node->parent;
  }
}

static AVLNode *avl_del(AVLNode *node) {
  if (node->right == NULL) {
    AVLNode *parent = node->parent;
    if (node->left) {
      node->left->parent = parent;
    }
    if (parent) {
      (parent->left == node ? parent->left : parent->right) = node->left;
      return avl_fix(parent);
    } else {
      return node->left;
    }
  } else {
    AVLNode *victim = node->right;
    while (victim->left) {
      victim = victim->left;
    }
    AVLNode *root = avl_del(victim);

    *victim = *node;
    if (victim->left) {
      victim->left->parent = victim;
    }
    if (victim->right) {
      victim->right->parent = victim;
    }
    AVLNode *parent = node->parent;
    if (parent) {
      (parent->left == node ? parent->left : parent->right) = victim;
      return root;
    } else {
      return victim;
    }
  }
}