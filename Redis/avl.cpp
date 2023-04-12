#include "avl.h"

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
static AVLNode *rot_left(AVLNode *node) {
  AVLNode *new_node = node->right;
  if (new_node->left) {
    new_node->left->parent = node;
  }
  node->right = new_node->left;
  new_node->left = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  avl_update(node);
  avl_update(new_node);
  return new_node;
}

/* 右旋操作, 将左子树 new_node 设为根节点
 * 1. 将 new_node 的右子树设为 node 的左子树
 * 2. 将 node 设为 new_node 的右子树
 * 3. 调整 node 和 new_node 的深度以及大小 */
static AVLNode *rot_right(AVLNode *node) {
  AVLNode *new_node = node->left;
  if (new_node->right) {
    new_node->right->parent = node;
  }
  node->left = new_node->right;
  new_node->right = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  avl_update(node);
  avl_update(new_node);
  return new_node;
}

/* 左子树深，需要右旋。如果是 LR 型先左旋为 LL 型 */
static AVLNode *avl_fix_left(AVLNode *root) {
  if (avl_depth(root->left->left) < avl_depth(root->left->right)) {
    root->left = rot_left(root->left);
  }
  return rot_right(root);
}

/* 右子树深，需要左旋。如果是 RL 型选右旋为 RR 型 */
static AVLNode *avl_fix_right(AVLNode *root) {
  if (avl_depth(root->right->right) < avl_depth(root->right->left)) {
    root->right = rot_right(root->right);
  }
  return rot_left(root);
}

/* 递归的调用 avl_fix_left 或者 avl-fix-right 直到 root 节点 */
AVLNode *avl_fix(AVLNode *node) {
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

/* 如果右子树为 NULL, 直接将用左子树替换 node 然后返回
 * 如果右子树有数据，查找比 node->val 大的第一个元素（右子树中最左的节点）
 * 将这个节点 victim 从 tree 中删除, 然后将 node 存到 victim 的位置上
 * 将 node 的子树的 parent 设为这个地址, 将 parent 的子树也设为这个地址 */
AVLNode *avl_del(AVLNode *node) {
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
    /* victim 被删除了，因此这块空间会被回收，但是真正需要删除的内容是 node
     * 把 node 存放在 victim 的空间上，那么 node 就被删除了 */
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

/* 查找 node 偏移 offset 的那个节点 */
AVLNode *avl_offset(AVLNode *node, int64_t offset) {
  int64_t pos = 0;
  while (offset != pos) {
    if (pos < offset && avl_cnt(node->right) >= offset - pos) {
      node = node->right;
      pos += avl_cnt(node->left) + 1;
    } else if (pos > offset && pos - avl_cnt(node->left) <= offset) {
      node = node->left;
      pos -= avl_cnt(node->right) + 1;
    } else {
      AVLNode *parent = node->parent;
      if (!parent) {
        return NULL;
      }
      if (parent->right == node) {
        pos -= avl_cnt(node->left) + 1;
      } else {
        pos += avl_cnt(node->right) + 1;
      }
      node = parent;
    }
  }
  return node;
}
