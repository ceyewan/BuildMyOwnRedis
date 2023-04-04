# Build My Own Redis


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

使用上面实现的 AVL 树, 来实现 Sorted-Set, 也即保持数据的有序性. 在 Redis 中, 是使用的跳表来实现.

