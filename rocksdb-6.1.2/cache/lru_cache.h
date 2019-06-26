//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#pragma once

#include <string>

#include "cache/sharded_cache.h"

#include "port/port.h"
#include "util/autovector.h"

namespace rocksdb {

// LRU cache implementation

// An entry is a variable length heap-allocated structure.
// Entries are referenced by cache and/or by any external entity.
// The cache keeps all its entries in table. Some elements
// are also stored on LRU list.
//
// LRUHandle can be in these states:
// 1. Referenced externally AND in hash table.
//  In that case the entry is *not* in the LRU. (refs > 1 && in_cache == true)
// 2. Not referenced externally and in hash table. In that case the entry is
// in the LRU and can be freed. (refs == 1 && in_cache == true)
// 3. Referenced externally and not in hash table. In that case the entry is
// in not on LRU and not in table. (refs >= 1 && in_cache == false)
//
// All newly created LRUHandles are in state 1. If you call
// LRUCacheShard::Release
// on entry in state 1, it will go into state 2. To move from state 1 to
// state 3, either call LRUCacheShard::Erase or LRUCacheShard::Insert with the
// same key.
// To move from state 2 to state 1, use LRUCacheShard::Lookup.
// Before destruction, make sure that no handles are in state 1. This means
// that any successful LRUCacheShard::Lookup/LRUCacheShard::Insert have a
// matching
// RUCache::Release (to move into state 2) or LRUCacheShard::Erase (for state 3)

/*  LRUHandleΪʲô�ᱻͬʱ���ڹ�ϣ����˫������֮�У�
ע�⿴LookUp��ʵ�֣��������ʹ��������������ṩO(n)�Ĳ�ѯЧ�ʣ������ڲ�ѯʱ�������˹�ϣ��ʵ��O(1)�Ĳ�ѯ��
��ô���������ʹ�ù�ϣ���أ���Ȼ����ʵ��O(1)�Ĳ�ѯ����ȴ�޷����»���ڵ�ķ���ʱ�䡣������Ϊ�������԰���
�̶���˳�򱻱���������ϣ���еĽڵ��޷��ṩ�̶��ı���˳�򣨿���Resizeǰ�󣩡���ô���ɲ����Խ�����ʱ���¼
��Handle�У�Ȼ����ù�ϣ���������ȿ���ʵ��O(1)�Ĳ�ѯ���ֿ��Է���ظ��»����¼�ķ���ʱ�䣬�����գ����ǣ�
���û�а�����ʱ��������������������������ʱ��������ο��ٶ�λ����Щ�����¼Ҫ�������أ�����O(n)�Ĳ�ѯЧ�ʡ�
��ϣ����֧�������������ݽṹ�������޷�����������������ߴ�������ؽ����߽�ϣ�ȡ�����̣����ù�ϣ��ʵ��O(1)
�Ĳ�ѯ����������ά�ֶԻ����¼������ʱ������	

        ��ѯ	����	ɾ��	����
����	O(n)	O(1)	O(1)	֧��
��ϣ��	O(1)	O(1)	O(1)	��֧��

ע1����ϣ��ʵ��O(1)������ǰ���ǣ�ƽ��ÿ��ϣͰԪ���� <= 1
ע2��Ϊ�˱���ƽ����ϣͰԪ��������Ҫʱ����Resize����Resize��ԭ��˳�򽫱�����
*/

//һ��LRUHandle����һ����㣬����ṹ����Ƶ�����֮�����ڣ����ȿ�����ΪHashTable�еĽ��
//LRUHandleTable.list_��Ա
struct LRUHandle { //��Ա��ֵ��LRUCacheShard::Insert
  void* value;
  //ɾ��������refs == 0ʱ������deleter���value�����ͷš�
  void (*deleter)(const Slice&, void* value);
  // ��ΪHashTable�еĽڵ㣬ָ��hashֵ��ͬ�Ľڵ㣨���hash��ͻ��������ַ�����������������û������ͬhash��KV�ڵ��ͻ����ͬhashֵ��ͨ���õ���������������
  //��ֵ�ο�LRUHandleTable::Insert  LRUHandleTable::Resize
  LRUHandle* next_hash; 

  //LRUHandle��hashͰ��listһ�������һ������LRU��̭�ã�һ�����ڿ��ٲ��ң����ǽڵ�LRUHandle�ǹ��õ�

  //LRU��أ�����ͷʵ����ΪLRUCacheShard.lru_��Ա�����Բο�https://blog.csdn.net/caoshangpa/article/details/78783749
  // ��ΪLRUCache�еĽڵ㣬ָ����
  LRUHandle* next;
  // ��ΪLRUCache�еĽڵ㣬ָ��ǰ��
  LRUHandle* prev;
   // �û�ָ��ռ�û���Ĵ�С
  size_t charge;  // TODO(opt): Only allow uint32_t?
  size_t key_length;//key_length key����,key������ʵ��ַkey_data
  /* 1.Ϊ��Ҫά�����ü�����
    ��Insertʱ�����ü�����ʼ��Ϊ2��һ���Ǹ�LRUCache��������ʱ�ã�һ���Ǹ��ⲿ����Release��Eraseʱ�á�Insertʱ������
  �����²���Ľ�㣬������ɺ���Ҫ���øý��Release��Erase���������ü�����1����ô��ʱ�ý������ü�������1�ˡ�
  ��LRUCache����ʱ�����Ƚ��������ü����ټ�1�������ʱ���ü���Ϊ0�������deleter�������ý�㳹�״��ڴ���free����
  Lookupʱ��������ҽӽ����ڣ���ʱ���ü������1��Ҳ���Ǳ����3����ʱ�����û����иý����ڼ䣬�û�����ܱ�ɾ������
  ��ԭ���磺�������������������ա�������ͬkey���»�����롢�������汻�����ȣ��������û����ʵ��Ƿ��ڴ棬���������
  ��ˣ���Ҫʹ�����ü���Ҫ��1��ά�������������ڡ���ΪLookup���ص����ҵ��Ľ�㣬�û��ڲ�����ɺ�Ҫ�������øý��
  ��Release��Erase��ʹ���ü������±��2��
  */
  // ���ü���
  uint32_t refs;     // a number of refs to this entry
                     // cache itself is counted as 1

  // Include the following flags:
  //   IN_CACHE:         whether this entry is referenced by the hash table.
  //   IS_HIGH_PRI:      whether this entry is high priority entry.
  //   IN_HIGH_PRI_POOL: whether this entry is in high-pri pool.
  //   HAS_HIT:          whether this entry has had any lookups (hits).
  enum Flags : uint8_t {
    IN_CACHE = (1 << 0),
    IS_HIGH_PRI = (1 << 1),
    IN_HIGH_PRI_POOL = (1 << 2),
    HAS_HIT = (1 << 3),
  };

  uint8_t flags;
  // ��ϣֵ
  uint32_t hash;     // Hash of key(); used for fast sharding and comparisons
  //key_length key����,key������ʵ��ַkey_data
  char key_data[1];  // Beginning of key

  Slice key() const {
    // For cheaper lookups, we allow a temporary Handle object
    // to store a pointer to a key in "value".
    if (next == this) {
      return *(reinterpret_cast<Slice*>(value));
    } else {
      return Slice(key_data, key_length);
    }
  }

  bool InCache() const { return flags & IN_CACHE; }
  bool IsHighPri() const { return flags & IS_HIGH_PRI; }
  bool InHighPriPool() const { return flags & IN_HIGH_PRI_POOL; }
  bool HasHit() const { return flags & HAS_HIT; }

  void SetInCache(bool in_cache) {
    if (in_cache) {
      flags |= IN_CACHE;
    } else {
      flags &= ~IN_CACHE;
    }
  }

  void SetPriority(Cache::Priority priority) {
    if (priority == Cache::Priority::HIGH) {
      flags |= IS_HIGH_PRI;
    } else {
      flags &= ~IS_HIGH_PRI;
    }
  }

  void SetInHighPriPool(bool in_high_pri_pool) {
    if (in_high_pri_pool) {
      flags |= IN_HIGH_PRI_POOL;
    } else {
      flags &= ~IN_HIGH_PRI_POOL;
    }
  }

  void SetHit() { flags |= HAS_HIT; }

  void Free() {
    assert((refs == 1 && InCache()) || (refs == 0 && !InCache()));
    if (deleter) {
      (*deleter)(key(), value);
    }
    delete[] reinterpret_cast<char*>(this);
  }
};

// We provide our own simple hash table since it removes a whole bunch
// of porting hacks and is also faster than some of the built-in hash
// table implementations in some of the compiler/runtime combinations
// we have tested.  E.g., readrandom speeds up by ~5% over the g++
// 4.4.3's builtin hashtable.

//LRUCacheShard.table_��Ա
class LRUHandleTable {
 public:
  LRUHandleTable();
  ~LRUHandleTable();

  LRUHandle* Lookup(const Slice& key, uint32_t hash);
  LRUHandle* Insert(LRUHandle* h);
  LRUHandle* Remove(const Slice& key, uint32_t hash);

  template <typename T>
  void ApplyToAllCacheEntries(T func) {
    for (uint32_t i = 0; i < length_; i++) {
      LRUHandle* h = list_[i];
      while (h != nullptr) {
        auto n = h->next_hash;
        assert(h->InCache());
        func(h);
        h = n;
      }
    }
  }

 private:
  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  LRUHandle** FindPointer(const Slice& key, uint32_t hash);

  void Resize();

  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  //hashͰ��������Ա��ֵLRUHandleTable::Resize
   //��ϣ��ַ����Ķ���ָ��
  LRUHandle** list_; 
  //��ϣ��ַ����ĳ���
  uint32_t length_;  
  //��ϣ�������н�������
  uint32_t elems_;  
};

// A single shard of sharded cache.
//LRUCache.shards_��Ա
class ALIGN_AS(CACHE_LINE_SIZE) LRUCacheShard final : public CacheShard {
 public:
  LRUCacheShard(size_t capacity, bool strict_capacity_limit,
                double high_pri_pool_ratio, bool use_adaptive_mutex);
  virtual ~LRUCacheShard();

  // Separate from constructor so caller can easily make an array of LRUCache
  // if current usage is more than new capacity, the function will attempt to
  // free the needed space
  virtual void SetCapacity(size_t capacity) override;

  // Set the flag to reject insertion if cache if full.
  virtual void SetStrictCapacityLimit(bool strict_capacity_limit) override;

  // Set percentage of capacity reserved for high-pri cache entries.
  void SetHighPriorityPoolRatio(double high_pri_pool_ratio);

  // Like Cache methods, but with an extra "hash" parameter.
  virtual Status Insert(const Slice& key, uint32_t hash, void* value,
                        size_t charge,
                        void (*deleter)(const Slice& key, void* value),
                        Cache::Handle** handle,
                        Cache::Priority priority) override;
  virtual Cache::Handle* Lookup(const Slice& key, uint32_t hash) override;
  virtual bool Ref(Cache::Handle* handle) override;
  virtual bool Release(Cache::Handle* handle,
                       bool force_erase = false) override;
  virtual void Erase(const Slice& key, uint32_t hash) override;

  // Although in some platforms the update of size_t is atomic, to make sure
  // GetUsage() and GetPinnedUsage() work correctly under any platform, we'll
  // protect them with mutex_.

  virtual size_t GetUsage() const override;
  virtual size_t GetPinnedUsage() const override;

  virtual void ApplyToAllCacheEntries(void (*callback)(void*, size_t),
                                      bool thread_safe) override;

  virtual void EraseUnRefEntries() override;

  virtual std::string GetPrintableOptions() const override;

  void TEST_GetLRUList(LRUHandle** lru, LRUHandle** lru_low_pri);

  //  Retrieves number of elements in LRU, for unit test purpose only
  //  not threadsafe
  size_t TEST_GetLRUSize();

  //  Retrives high pri pool ratio
  double GetHighPriPoolRatio();

 private:
  void LRU_Remove(LRUHandle* e);
  void LRU_Insert(LRUHandle* e);

  // Overflow the last entry in high-pri pool to low-pri pool until size of
  // high-pri pool is no larger than the size specify by high_pri_pool_pct.
  void MaintainPoolSize();

  // Just reduce the reference count by 1.
  // Return true if last reference
  bool Unref(LRUHandle* e);

  // Free some space following strict LRU policy until enough space
  // to hold (usage_ + charge) is freed or the lru list is empty
  // This function is not thread safe - it needs to be executed while
  // holding the mutex_
  void EvictFromLRU(size_t charge, autovector<LRUHandle*>* deleted);

  // Initialized before use.
  size_t capacity_;

  // Memory size for entries in high-pri pool.
  size_t high_pri_pool_usage_;

  // Whether to reject insertion if cache reaches its full capacity.
  bool strict_capacity_limit_;

  // Ratio of capacity reserved for high priority cache entries.
  double high_pri_pool_ratio_;

  // High-pri pool size, equals to capacity * high_pri_pool_ratio.
  // Remember the value to avoid recomputing each time.
  double high_pri_pool_capacity_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  // LRU contains items which can be evicted, ie reference only by cache
  LRUHandle lru_;

  // Pointer to head of low-pri pool in LRU list.
  LRUHandle* lru_low_pri_;

  // ------------^^^^^^^^^^^^^-----------
  // Not frequently modified data members
  // ------------------------------------
  //
  // We separate data members that are updated frequently from the ones that
  // are not frequently updated so that they don't share the same cache line
  // which will lead into false cache sharing
  //
  // ------------------------------------
  // Frequently modified data members
  // ------------vvvvvvvvvvvvv-----------
  //һ��LRUCacheShard���Ӧһ�� LRU hashͰ
  LRUHandleTable table_;

  // Memory size for entries residing in the cache
  size_t usage_;

  // Memory size for entries residing only in the LRU list
  size_t lru_usage_;

  // mutex_ protects the following state.
  // We don't count mutex_ as the cache's internal state so semantically we
  // don't mind mutex_ invoking the non-const actions.
  mutable port::Mutex mutex_;
};

//LRUCache�̳�ShardedCache��ShardedCache�̳�Cache
//ͼ��ο�https://blog.csdn.net/caoshangpa/article/details/78960999
class LRUCache
#ifdef NDEBUG
    final
#endif
    : public ShardedCache {
 public:
  LRUCache(size_t capacity, int num_shard_bits, bool strict_capacity_limit,
           double high_pri_pool_ratio,
           std::shared_ptr<MemoryAllocator> memory_allocator = nullptr,
           bool use_adaptive_mutex = kDefaultToAdaptiveMutex);
  virtual ~LRUCache();
  virtual const char* Name() const override { return "LRUCache"; }
  virtual CacheShard* GetShard(int shard) override;
  virtual const CacheShard* GetShard(int shard) const override;
  virtual void* Value(Handle* handle) override;
  virtual size_t GetCharge(Handle* handle) const override;
  virtual uint32_t GetHash(Handle* handle) const override;
  virtual void DisownData() override;

  //  Retrieves number of elements in LRU, for unit test purpose only
  size_t TEST_GetLRUSize();
  //  Retrives high pri pool ratio
  double GetHighPriPoolRatio();

 private:
 
/*  
SharedLRUCache������ʲô�أ�
    ����Ϊʲô��Ҫ����������ΪlevelDB�Ƕ��̵߳ģ�ÿ���̷߳��ʻ�������ʱ�򶼻Ὣ��������ס��Ϊ�˶��̷߳��ʣ������ܿ��٣�
������������ShardedLRUCache�ڲ���16��LRUCache������Keyʱ���ȼ���key������һ����Ƭ����Ƭ�ļ��㷽����ȡ32λhashֵ�ĸ�4λ��
Ȼ������Ӧ��LRUCache�н��в��ң������ʹ������˶��̵߳ķ������Ŀ�����
*/
 //LRUCache::LRUCache
  LRUCacheShard* shards_ = nullptr;
  //���ٸ�LRU hashͰ
  int num_shards_ = 0;
};

}  // namespace rocksdb