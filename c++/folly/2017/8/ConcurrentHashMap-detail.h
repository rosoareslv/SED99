/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/experimental/hazptr/hazptr.h>
#include <atomic>
#include <mutex>

namespace folly {

namespace detail {

namespace concurrenthashmap {

// hazptr retire() that can use an allocator.
template <typename Allocator>
class HazptrDeleter {
 public:
  template <typename Node>
  void operator()(Node* node) {
    node->~Node();
    Allocator().deallocate((uint8_t*)node, sizeof(Node));
  }
};

template <
    typename KeyType,
    typename ValueType,
    typename Allocator,
    typename Enabled = void>
class ValueHolder {
 public:
  typedef std::pair<const KeyType, ValueType> value_type;

  explicit ValueHolder(const ValueHolder& other) : item_(other.item_) {}

  template <typename... Args>
  ValueHolder(const KeyType& k, Args&&... args)
      : item_(
            std::piecewise_construct,
            std::forward_as_tuple(k),
            std::forward_as_tuple(std::forward<Args>(args)...)) {}
  value_type& getItem() {
    return item_;
  }

 private:
  value_type item_;
};

// If the ValueType is not copy constructible, we can instead add
// an extra indirection.  Adds more allocations / deallocations and
// pulls in an extra cacheline.
template <typename KeyType, typename ValueType, typename Allocator>
class ValueHolder<
    KeyType,
    ValueType,
    Allocator,
    std::enable_if_t<!std::is_nothrow_copy_constructible<ValueType>::value>> {
 public:
  typedef std::pair<const KeyType, ValueType> value_type;

  explicit ValueHolder(const ValueHolder& other) {
    other.owned_ = false;
    item_ = other.item_;
  }

  template <typename... Args>
  ValueHolder(const KeyType& k, Args&&... args) {
    item_ = (value_type*)Allocator().allocate(sizeof(value_type));
    new (item_) value_type(
        std::piecewise_construct,
        std::forward_as_tuple(k),
        std::forward_as_tuple(std::forward<Args>(args)...));
  }

  ~ValueHolder() {
    if (owned_) {
      item_->~value_type();
      Allocator().deallocate((uint8_t*)item_, sizeof(value_type));
    }
  }

  value_type& getItem() {
    return *item_;
  }

 private:
  value_type* item_;
  mutable bool owned_{true};
};

template <
    typename KeyType,
    typename ValueType,
    typename Allocator,
    template <typename> class Atom = std::atomic>
class NodeT : public folly::hazptr::hazptr_obj_base<
                  NodeT<KeyType, ValueType, Allocator, Atom>,
                  concurrenthashmap::HazptrDeleter<Allocator>> {
 public:
  typedef std::pair<const KeyType, ValueType> value_type;

  explicit NodeT(NodeT* other) : item_(other->item_) {}

  template <typename... Args>
  NodeT(const KeyType& k, Args&&... args)
      : item_(k, std::forward<Args>(args)...) {}

  /* Nodes are refcounted: If a node is retired() while a writer is
     traversing the chain, the rest of the chain must remain valid
     until all readers are finished.  This includes the shared tail
     portion of the chain, as well as both old/new hash buckets that
     may point to the same portion, and erased nodes may increase the
     refcount */
  void acquire() {
    DCHECK(refcount_.load() != 0);
    refcount_.fetch_add(1);
  }
  void release() {
    if (refcount_.fetch_sub(1) == 1 /* was previously 1 */) {
      this->retire(
          folly::hazptr::default_hazptr_domain(),
          concurrenthashmap::HazptrDeleter<Allocator>());
    }
  }
  ~NodeT() {
    auto next = next_.load(std::memory_order_acquire);
    if (next) {
      next->release();
    }
  }

  value_type& getItem() {
    return item_.getItem();
  }
  Atom<NodeT*> next_{nullptr};

 private:
  ValueHolder<KeyType, ValueType, Allocator> item_;
  Atom<uint8_t> refcount_{1};
};

} // namespace concurrenthashmap

/* A Segment is a single shard of the ConcurrentHashMap.
 * All writes take the lock, while readers are all wait-free.
 * Readers always proceed in parallel with the single writer.
 *
 *
 * Possible additional optimizations:
 *
 * * insert / erase could be lock / wait free.  Would need to be
 *   careful that assign and rehash don't conflict (possibly with
 *   reader/writer lock, or microlock per node or per bucket, etc).
 *   Java 8 goes halfway, and and does lock per bucket, except for the
 *   first item, that is inserted with a CAS (which is somewhat
 *   specific to java having a lock per object)
 *
 * * I tried using trylock() and find() to warm the cache for insert()
 *   and erase() similar to Java 7, but didn't have much luck.
 *
 * * We could order elements using split ordering, for faster rehash,
 *   and no need to ever copy nodes.  Note that a full split ordering
 *   including dummy nodes increases the memory usage by 2x, but we
 *   could split the difference and still require a lock to set bucket
 *   pointers.
 *
 * * hazptr acquire/release could be optimized more, in
 *   single-threaded case, hazptr overhead is ~30% for a hot find()
 *   loop.
 */
template <
    typename KeyType,
    typename ValueType,
    uint8_t ShardBits = 0,
    typename HashFn = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>,
    typename Allocator = std::allocator<uint8_t>,
    template <typename> class Atom = std::atomic,
    class Mutex = std::mutex>
class FOLLY_ALIGNED(64) ConcurrentHashMapSegment {
  enum class InsertType {
    DOES_NOT_EXIST, // insert/emplace operations.  If key exists, return false.
    MUST_EXIST, // assign operations.  If key does not exist, return false.
    ANY, // insert_or_assign.
    MATCH, // assign_if_equal (not in std).  For concurrent maps, a
           // way to atomically change a value if equal to some other
           // value.
  };

 public:
  typedef KeyType key_type;
  typedef ValueType mapped_type;
  typedef std::pair<const KeyType, ValueType> value_type;
  typedef std::size_t size_type;

  using Node = concurrenthashmap::NodeT<KeyType, ValueType, Allocator, Atom>;
  class Iterator;

  ConcurrentHashMapSegment(
      size_t initial_buckets,
      float load_factor,
      size_t max_size)
      : load_factor_(load_factor) {
    auto buckets = (Buckets*)Allocator().allocate(sizeof(Buckets));
    initial_buckets = folly::nextPowTwo(initial_buckets);
    if (max_size != 0) {
      max_size_ = folly::nextPowTwo(max_size);
    }
    if (max_size_ > max_size) {
      max_size_ >> 1;
    }

    CHECK(max_size_ == 0 || (folly::popcount(max_size_ - 1) + ShardBits <= 32));
    new (buckets) Buckets(initial_buckets);
    buckets_.store(buckets, std::memory_order_release);
    load_factor_nodes_ = initial_buckets * load_factor_;
  }

  ~ConcurrentHashMapSegment() {
    auto buckets = buckets_.load(std::memory_order_relaxed);
    // We can delete and not retire() here, since users must have
    // their own synchronization around destruction.
    buckets->~Buckets();
    Allocator().deallocate((uint8_t*)buckets, sizeof(Buckets));
  }

  size_t size() {
    return size_;
  }

  bool empty() {
    return size() == 0;
  }

  bool insert(Iterator& it, std::pair<key_type, mapped_type>&& foo) {
    return insert(it, foo.first, foo.second);
  }

  bool insert(Iterator& it, const KeyType& k, const ValueType& v) {
    auto node = (Node*)Allocator().allocate(sizeof(Node));
    new (node) Node(k, v);
    auto res = insert_internal(
        it,
        k,
        InsertType::DOES_NOT_EXIST,
        [](const ValueType&) { return false; },
        node,
        v);
    if (!res) {
      node->~Node();
      Allocator().deallocate((uint8_t*)node, sizeof(Node));
    }
    return res;
  }

  template <typename... Args>
  bool try_emplace(Iterator& it, const KeyType& k, Args&&... args) {
    return insert_internal(
        it,
        k,
        InsertType::DOES_NOT_EXIST,
        [](const ValueType&) { return false; },
        nullptr,
        std::forward<Args>(args)...);
  }

  template <typename... Args>
  bool emplace(Iterator& it, const KeyType& k, Node* node) {
    return insert_internal(
        it,
        k,
        InsertType::DOES_NOT_EXIST,
        [](const ValueType&) { return false; },
        node);
  }

  bool insert_or_assign(Iterator& it, const KeyType& k, const ValueType& v) {
    return insert_internal(
        it,
        k,
        InsertType::ANY,
        [](const ValueType&) { return false; },
        nullptr,
        v);
  }

  bool assign(Iterator& it, const KeyType& k, const ValueType& v) {
    auto node = (Node*)Allocator().allocate(sizeof(Node));
    new (node) Node(k, v);
    auto res = insert_internal(
        it,
        k,
        InsertType::MUST_EXIST,
        [](const ValueType&) { return false; },
        node,
        v);
    if (!res) {
      node->~Node();
      Allocator().deallocate((uint8_t*)node, sizeof(Node));
    }
    return res;
  }

  bool assign_if_equal(
      Iterator& it,
      const KeyType& k,
      const ValueType& expected,
      const ValueType& desired) {
    return insert_internal(
        it,
        k,
        InsertType::MATCH,
        [expected](const ValueType& v) { return v == expected; },
        nullptr,
        desired);
  }

  template <typename MatchFunc, typename... Args>
  bool insert_internal(
      Iterator& it,
      const KeyType& k,
      InsertType type,
      MatchFunc match,
      Node* cur,
      Args&&... args) {
    auto h = HashFn()(k);
    std::unique_lock<Mutex> g(m_);

    auto buckets = buckets_.load(std::memory_order_relaxed);
    // Check for rehash needed for DOES_NOT_EXIST
    if (size_ >= load_factor_nodes_ && type == InsertType::DOES_NOT_EXIST) {
      if (max_size_ && size_ << 1 > max_size_) {
        // Would exceed max size.
        throw std::bad_alloc();
      }
      rehash(buckets->bucket_count_ << 1);
      buckets = buckets_.load(std::memory_order_relaxed);
    }

    auto idx = getIdx(buckets, h);
    auto head = &buckets->buckets_[idx];
    auto node = head->load(std::memory_order_relaxed);
    auto headnode = node;
    auto prev = head;
    it.buckets_hazptr_.reset(buckets);
    while (node) {
      // Is the key found?
      if (KeyEqual()(k, node->getItem().first)) {
        it.setNode(node, buckets, idx);
        it.node_hazptr_.reset(node);
        if (type == InsertType::MATCH) {
          if (!match(node->getItem().second)) {
            return false;
          }
        }
        if (type == InsertType::DOES_NOT_EXIST) {
          return false;
        } else {
          if (!cur) {
            cur = (Node*)Allocator().allocate(sizeof(Node));
            new (cur) Node(k, std::forward<Args>(args)...);
          }
          auto next = node->next_.load(std::memory_order_relaxed);
          cur->next_.store(next, std::memory_order_relaxed);
          if (next) {
            next->acquire();
          }
          prev->store(cur, std::memory_order_release);
          g.unlock();
          // Release not under lock.
          node->release();
          return true;
        }
      }

      prev = &node->next_;
      node = node->next_.load(std::memory_order_relaxed);
    }
    if (type != InsertType::DOES_NOT_EXIST && type != InsertType::ANY) {
      it.node_hazptr_.reset();
      it.buckets_hazptr_.reset();
      return false;
    }
    // Node not found, check for rehash on ANY
    if (size_ >= load_factor_nodes_ && type == InsertType::ANY) {
      if (max_size_ && size_ << 1 > max_size_) {
        // Would exceed max size.
        throw std::bad_alloc();
      }
      rehash(buckets->bucket_count_ << 1);

      // Reload correct bucket.
      buckets = buckets_.load(std::memory_order_relaxed);
      it.buckets_hazptr_.reset(buckets);
      idx = getIdx(buckets, h);
      head = &buckets->buckets_[idx];
      headnode = head->load(std::memory_order_relaxed);
    }

    // We found a slot to put the node.
    size_++;
    if (!cur) {
      // InsertType::ANY
      // OR DOES_NOT_EXIST, but only in the try_emplace case
      DCHECK(type == InsertType::ANY || type == InsertType::DOES_NOT_EXIST);
      cur = (Node*)Allocator().allocate(sizeof(Node));
      new (cur) Node(k, std::forward<Args>(args)...);
    }
    cur->next_.store(headnode, std::memory_order_relaxed);
    head->store(cur, std::memory_order_release);
    it.setNode(cur, buckets, idx);
    return true;
  }

  // Must hold lock.
  void rehash(size_t bucket_count) {
    auto buckets = buckets_.load(std::memory_order_relaxed);
    auto newbuckets = (Buckets*)Allocator().allocate(sizeof(Buckets));
    new (newbuckets) Buckets(bucket_count);

    load_factor_nodes_ = bucket_count * load_factor_;

    for (size_t i = 0; i < buckets->bucket_count_; i++) {
      auto bucket = &buckets->buckets_[i];
      auto node = bucket->load(std::memory_order_relaxed);
      if (!node) {
        continue;
      }
      auto h = HashFn()(node->getItem().first);
      auto idx = getIdx(newbuckets, h);
      // Reuse as long a chain as possible from the end.  Since the
      // nodes don't have previous pointers, the longest last chain
      // will be the same for both the previous hashmap and the new one,
      // assuming all the nodes hash to the same bucket.
      auto lastrun = node;
      auto lastidx = idx;
      auto count = 0;
      auto last = node->next_.load(std::memory_order_relaxed);
      for (; last != nullptr;
           last = last->next_.load(std::memory_order_relaxed)) {
        auto k = getIdx(newbuckets, HashFn()(last->getItem().first));
        if (k != lastidx) {
          lastidx = k;
          lastrun = last;
          count = 0;
        }
        count++;
      }
      // Set longest last run in new bucket, incrementing the refcount.
      lastrun->acquire();
      newbuckets->buckets_[lastidx].store(lastrun, std::memory_order_relaxed);
      // Clone remaining nodes
      for (; node != lastrun;
           node = node->next_.load(std::memory_order_relaxed)) {
        auto newnode = (Node*)Allocator().allocate(sizeof(Node));
        new (newnode) Node(node);
        auto k = getIdx(newbuckets, HashFn()(node->getItem().first));
        auto prevhead = &newbuckets->buckets_[k];
        newnode->next_.store(prevhead->load(std::memory_order_relaxed));
        prevhead->store(newnode, std::memory_order_relaxed);
      }
    }

    auto oldbuckets = buckets_.load(std::memory_order_relaxed);
    buckets_.store(newbuckets, std::memory_order_release);
    oldbuckets->retire(
        folly::hazptr::default_hazptr_domain(),
        concurrenthashmap::HazptrDeleter<Allocator>());
  }

  bool find(Iterator& res, const KeyType& k) {
    folly::hazptr::hazptr_holder haznext;
    auto h = HashFn()(k);
    auto buckets = res.buckets_hazptr_.get_protected(buckets_);
    auto idx = getIdx(buckets, h);
    auto prev = &buckets->buckets_[idx];
    auto node = res.node_hazptr_.get_protected(*prev);
    while (node) {
      if (KeyEqual()(k, node->getItem().first)) {
        res.setNode(node, buckets, idx);
        return true;
      }
      node = haznext.get_protected(node->next_);
      haznext.swap(res.node_hazptr_);
    }
    return false;
  }

  // Listed separately because we need a prev pointer.
  size_type erase(const key_type& key) {
    return erase_internal(key, nullptr);
  }

  size_type erase_internal(const key_type& key, Iterator* iter) {
    Node* node{nullptr};
    auto h = HashFn()(key);
    {
      std::lock_guard<Mutex> g(m_);

      auto buckets = buckets_.load(std::memory_order_relaxed);
      auto idx = getIdx(buckets, h);
      auto head = &buckets->buckets_[idx];
      node = head->load(std::memory_order_relaxed);
      Node* prev = nullptr;
      auto headnode = node;
      while (node) {
        if (KeyEqual()(key, node->getItem().first)) {
          auto next = node->next_.load(std::memory_order_relaxed);
          if (next) {
            next->acquire();
          }
          if (prev) {
            prev->next_.store(next, std::memory_order_release);
          } else {
            // Must be head of list.
            head->store(next, std::memory_order_release);
          }

          if (iter) {
            iter->buckets_hazptr_.reset(buckets);
            iter->setNode(
                node->next_.load(std::memory_order_acquire), buckets, idx);
          }
          size_--;
          break;
        }
        prev = node;
        node = node->next_.load(std::memory_order_relaxed);
      }
    }
    // Delete the node while not under the lock.
    if (node) {
      node->release();
      return 1;
    }
    DCHECK(!iter);

    return 0;
  }

  // Unfortunately because we are reusing nodes on rehash, we can't
  // have prev pointers in the bucket chain.  We have to start the
  // search from the bucket.
  //
  // This is a small departure from standard stl containers: erase may
  // throw if hash or key_eq functions throw.
  void erase(Iterator& res, Iterator& pos) {
    auto cnt = erase_internal(pos->first, &res);
    DCHECK(cnt == 1);
  }

  void clear() {
    auto buckets = buckets_.load(std::memory_order_relaxed);
    auto newbuckets = (Buckets*)Allocator().allocate(sizeof(Buckets));
    new (newbuckets) Buckets(buckets->bucket_count_);
    {
      std::lock_guard<Mutex> g(m_);
      buckets_.store(newbuckets, std::memory_order_release);
      size_ = 0;
    }
    buckets->retire(
        folly::hazptr::default_hazptr_domain(),
        concurrenthashmap::HazptrDeleter<Allocator>());
  }

  void max_load_factor(float factor) {
    std::lock_guard<Mutex> g(m_);
    load_factor_ = factor;
    auto buckets = buckets_.load(std::memory_order_relaxed);
    load_factor_nodes_ = buckets->bucket_count_ * load_factor_;
  }

  Iterator cbegin() {
    Iterator res;
    auto buckets = res.buckets_hazptr_.get_protected(buckets_);
    res.setNode(nullptr, buckets, 0);
    res.next();
    return res;
  }

  Iterator cend() {
    return Iterator(nullptr);
  }

  // Could be optimized to avoid an extra pointer dereference by
  // allocating buckets_ at the same time.
  class Buckets : public folly::hazptr::hazptr_obj_base<
                      Buckets,
                      concurrenthashmap::HazptrDeleter<Allocator>> {
   public:
    explicit Buckets(size_t count) : bucket_count_(count) {
      buckets_ =
          (Atom<Node*>*)Allocator().allocate(sizeof(Atom<Node*>) * count);
      new (buckets_) Atom<Node*>[ count ];
      for (size_t i = 0; i < count; i++) {
        buckets_[i].store(nullptr, std::memory_order_relaxed);
      }
    }
    ~Buckets() {
      for (size_t i = 0; i < bucket_count_; i++) {
        auto elem = buckets_[i].load(std::memory_order_relaxed);
        if (elem) {
          elem->release();
        }
      }
      Allocator().deallocate(
          (uint8_t*)buckets_, sizeof(Atom<Node*>) * bucket_count_);
    }

    size_t bucket_count_;
    Atom<Node*>* buckets_{nullptr};
  };

 public:
  class Iterator {
   public:
    FOLLY_ALWAYS_INLINE Iterator() {}
    FOLLY_ALWAYS_INLINE explicit Iterator(std::nullptr_t)
        : buckets_hazptr_(nullptr), node_hazptr_(nullptr) {}
    FOLLY_ALWAYS_INLINE ~Iterator() {}

    void setNode(Node* node, Buckets* buckets, uint64_t idx) {
      node_ = node;
      buckets_ = buckets;
      idx_ = idx;
    }

    const value_type& operator*() const {
      DCHECK(node_);
      return node_->getItem();
    }

    const value_type* operator->() const {
      DCHECK(node_);
      return &(node_->getItem());
    }

    const Iterator& operator++() {
      DCHECK(node_);
      node_ = node_hazptr_.get_protected(node_->next_);
      if (!node_) {
        ++idx_;
        next();
      }
      return *this;
    }

    void next() {
      while (!node_) {
        if (idx_ >= buckets_->bucket_count_) {
          break;
        }
        DCHECK(buckets_);
        DCHECK(buckets_->buckets_);
        node_ = node_hazptr_.get_protected(buckets_->buckets_[idx_]);
        if (node_) {
          break;
        }
        ++idx_;
      }
    }

    Iterator operator++(int) {
      auto prev = *this;
      ++*this;
      return prev;
    }

    bool operator==(const Iterator& o) const {
      return node_ == o.node_;
    }

    bool operator!=(const Iterator& o) const {
      return !(*this == o);
    }

    Iterator& operator=(const Iterator& o) {
      node_ = o.node_;
      node_hazptr_.reset(node_);
      idx_ = o.idx_;
      buckets_ = o.buckets_;
      buckets_hazptr_.reset(buckets_);
      return *this;
    }

    /* implicit */ Iterator(const Iterator& o) {
      node_ = o.node_;
      node_hazptr_.reset(node_);
      idx_ = o.idx_;
      buckets_ = o.buckets_;
      buckets_hazptr_.reset(buckets_);
    }

    /* implicit */ Iterator(Iterator&& o) noexcept
        : buckets_hazptr_(std::move(o.buckets_hazptr_)),
          node_hazptr_(std::move(o.node_hazptr_)) {
      node_ = o.node_;
      buckets_ = o.buckets_;
      idx_ = o.idx_;
    }

    // These are accessed directly from the functions above
    folly::hazptr::hazptr_holder buckets_hazptr_;
    folly::hazptr::hazptr_holder node_hazptr_;

   private:
    Node* node_{nullptr};
    Buckets* buckets_{nullptr};
    uint64_t idx_;
  };

 private:
  // Shards have already used low ShardBits of the hash.
  // Shift it over to use fresh bits.
  uint64_t getIdx(Buckets* buckets, size_t hash) {
    return (hash >> ShardBits) & (buckets->bucket_count_ - 1);
  }

  float load_factor_;
  size_t load_factor_nodes_;
  size_t size_{0};
  size_t max_size_{0};
  Atom<Buckets*> buckets_{nullptr};
  Mutex m_;
};
} // namespace detail
} // namespace folly
