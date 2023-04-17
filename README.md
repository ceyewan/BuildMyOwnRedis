# Build My Own Redis

## 概述

使用 C++ 实现一个支持**动态字符串和有序集合**的简易 Redis，所有的代码均使用**面向过程的设计思想**编写。Redis 中的数据都在内存中，因此我们使用单线程执行，仅仅在 delete 有序集合时使用线程池来辅助。这是因为 IO 操作不会成为性能的瓶颈，而有序集合的 value 可能太大，阻塞主进程。

项目亮点：

- 使用侵入式数据结构，将数据结构和数据分离，具有更高的效率、更简洁的代码和更高的代码复用性。

- 使用 poll() 多路复用实现非阻塞事件循环。因为 epoll() 在 WebServer 中已经使用过了

- "协议化"字符串，使用 `len + str` 简单封装动态字符串，不依赖 `\0` 拆分字符串，更加的灵活、安全、高效

- 使用链表法实现 HashTable，支持动态扩容。将扩容操作分散在多次操作时，避免单次操作引发扩容长时间阻塞线程

- 使用 AVL 树来实现有序集合，AVL 树相比于跳表来说，实现难度较小。并借助哈希表实现 O(1) 复杂度的查找操作

- 基于**链表+哈希表**实现计时器，清理超时连接，减少 Redis 负载，降低系统资源占用

## 03-Hello-Server-Client

实现了一个基础的 C/S 模型.

## 04-Protocol-Parsing

我们的服务器将能够处理来自客户端的多个请求, 为此我们需要实现某种“协议”, 至少要将请求从 TCP 字节流中分离出来. 协议由两个部分组成：**一个 4 字节的小端整数，表示请求的长度，和一个可变长度的请求。**

协议的存在, 使得客户端可以连续发送请求, 服务端有能力区分不同的请求, 实现**流水线请求**.

## 05-Event-Loop-And-Nonblocking-IO

在服务器端网络编程中，有 3 种处理并发连接的方法。它们是: 多进程、多线程和事件循环。事件循环使用**轮询和非阻塞 IO**，通常在单个线程上运行。由于进程和线程的开销，大多数现代生产级软件都使用事件循环进行联网。

在这个部分, 我们使用 poll() 多路复用, 并设置 fd 为非阻塞. 

## 06-Event-Loop-Implementation

上一部分的代码实现

## 07-Basic-Server-Get/Set/Del

将客户端的请求约束为 get/set/del 这三种命令.

`nstr [len1 str1] [len2 str2] ... [len_nstr str_nstr]` 第一个整数表示后续消息的长度。消息中又分为不同的参数，参数分为参数的长度和参数本身。如 `3 3 set 1 k 1 v` 表示后续有 3 个参数.

至此, 我们的 Redis 已经支持了对 kv 键值对的操作了.

## 08-HashTable

使用链表法实现自己的 HashTable, 并通过第二个临时哈希, 支持扩容操作. 扩容的条件是达到最大负载因子, 在扩容时, 为了避免扩容数据转移造成的"卡了", 将数据转移分散为多次操作.

## 09-Data-Serialization

将响应修改为序列化协议, 这个协议有五种类型，当不需要响应（如 `set` 操作，或者 `get` 不存在的 `key`），返回 `SER_NIL` 表示 `NULL`；如果查询到了数据，返回 `SER_STR + str`；如果删除元素，返回 `SER_INT` 加一个整数表示是否成功；如果出错，返回 `SER_ERR + code + msg_len + msg`。

并且，我们还添加了一个 `keys` 命令，单独使用，查询所有的 `kv` 对。首先，返回一个 `SER_ARR` 和 `kv` 的长度 `len`，接下来返回 `len` 个 `SER_STR + str`。

服务端, 需要按照这种原理重新解析响应.

## 10-AVL-Tree

实现了一棵平衡二叉树, 支持 insert/delete/query 三种操作.

最大的难点在于左旋右旋保持树的平衡.

## 11-AVL-Tree-And-Sorted-Set

使用上面实现的 AVL 树, 来实现 Sorted-Set, 以 O(logN) 的插入删除复杂度保持数据的有序性. 在实际 Redis 的实现中, 是使用的跳表.

同时, 使用 HashTable 来优化数据的查找，复杂度从 O(logN) 降低到 O(1)。

## TTL + ThreadPool

基于链表实现了一个超时器，为了快速定位节点在链表中的位置，可以使用 HashTable 来处理。

对于 zset 的删除操作，因为 value 是一棵 AVL 树，删除很费时，所以需要使用线程池来操作，避免阻塞操作

## Redis

重构了一下代码，避免 server.cpp 函数太长太臭了。将 g_data 放在 common.h 中，为了避免重复定义，将静态变量声明为 extern，这将告诉编译器该变量在另一个文件中定义，并且在链接阶段将为该变量分配一个单一的地址。然后我们在 server.cpp 中定义 g_data 即可。

## Build

```shell
cd Redis
make
./server
```

接下来，可以执行如下指令操作 Redis

```shell
./client set k v
./client get k
./client del k
./client zadd zset 1.1 n1
./client zscore zset n1  # 1.1
./client zrem zset n1
./client zquery zset score name offset limit
```

也可以使用 test_cmds.py 脚本来测试 Redis
