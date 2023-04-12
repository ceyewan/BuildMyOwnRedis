#pragma once

#include "common.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

inline void dlist_init(DList *node) { node->prev = node->next = node; }

inline bool dlist_empty(DList *node) { return node->next == node; }

inline void dlist_detach(DList *node) {
  DList *prev = node->prev;
  DList *next = node->next;
  prev->next = next;
  next->prev = prev;
}

inline void dlist_insert_before(DList *target, DList *rookie) {
  DList *prev = target->prev;
  prev->next = rookie;
  rookie->prev = prev;
  rookie->next = target;
  target->prev = rookie;
}

inline uint64_t get_monotonic_usec() {
  struct timespec tv = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return uint64_t(tv.tv_sec) * 1000000 + tv.tv_nsec / 1000;
}

const uint64_t k_idle_timeout_ms = 5 * 1000;

inline uint32_t next_timer_ms() {
  if (dlist_empty(&g_data.idle_list)) {
    return 10000;
  }
  uint64_t now_us = get_monotonic_usec();
  Conn *next = container_of(g_data.idle_list.next, Conn, idle_list);
  uint64_t next_us = next->idle_start + k_idle_timeout_ms * 1000;
  if (next_us <= now_us) {
    return 0;
  }
  return (uint32_t)((next_us - now_us) / 1000);
}

typedef void (*call_back)(Conn *);

inline void process_timers(call_back cb) {
  uint64_t now_us = get_monotonic_usec();
  while (!dlist_empty(&g_data.idle_list)) {
    Conn *next = container_of(g_data.idle_list.next, Conn, idle_list);
    uint64_t next_us = next->idle_start + k_idle_timeout_ms * 1000;
    if (next_us >= now_us + 1000) {
      break;
    }
    printf("Removing idle connection: %d\n", next->fd);
    cb(next);
  }
}