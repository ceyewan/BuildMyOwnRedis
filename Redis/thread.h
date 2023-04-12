#pragma once

#include <cstddef>
#include <cstdint>
#include <pthread.h>
#include <queue>
#include <vector>

struct Work {
  void (*f)(void *) = NULL;
  void *arg = NULL;
};

struct ThreadPool {
  std::vector<pthread_t> threads;
  std::deque<Work> queue;
  pthread_mutex_t mtx;
  pthread_cond_t not_empty;
};

void thread_pool_init(ThreadPool *tp, size_t nums_threads);
void thread_pool_queue(ThreadPool *tp, void (*f)(void *), void *arg);