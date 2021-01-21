#include "table.h"

#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table *table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table *table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

// 删除操作，可能会导致最终么有空桶可以使用!!
static Entry *findEntry(Entry *entries, int capacity, ObjString *key) {
  // key->hash 为已经计算并缓存好的分布均匀的 hash 值，不需要重复计算。
  // 通过 hash 值 % capacity 使其分布到具体的位置。
  uint32_t index = key->hash % capacity;

  Entry *tombstone = NULL;

  for (;;) {  // 负载系数使得死循环的情况不会发生。
    Entry *entry = &entries[index];
    // 可能是被删除的值或者是压根就没有存过值在这里
    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        // 当找到一个真正空的 entry 时,此时可以确实 table 中不存在我想找到的
        // hash 值 那么可以推断出外界可以插入，此时如果查找到真空 entry
        // 之前遇到过墓碑节点 则把墓碑节点返回给外界使用
        return tombstone != NULL ? tombstone : entry;
      } else {
        if (tombstone == NULL) tombstone = entry;  // 找到墓碑并不停止
      }
      // 字符串串是一个 object,对大多数语言而言， ==
      // 比较的是对象的引用地址，而不是对象本身
      // == 比较的是字符串的地址而不是内容
      // 两个字符串的地址可能不相同但是对应的值相同。
      // c/java
      // 都满足上面的条件，但是后来的大多脚本形语言对于相同的字符串并不会重复的创建对象。而是使用一种称为
      // stirng interning 的技术
      // 让两个相同的字符串的引用地址也相同（既使用同一份地址的字符串）
      // interning is short for "internal" trem
    } else if (entry->key == key) {
      return entry;
    }

    // 继续去下一个桶，知道找到对应的值
    // 这里再 % 一次是避免值过大溢出
    // 如果 index + 1 < capacity, 则 index + 1 % capacity = index + 1 本身
    // 如果 index + 1 = capacity, 则 index = 0; 既从头开始
    index = (index + 1) % capacity;
  }
}

// value 相当于多类型值返回？
bool tableGet(Table *table, ObjString *key, Value *value) {
  if (table->count == 0) return false;

  Entry *entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

// 需要注意 hash 表调整大小时，如果原来的 hash 表中存在 key， 则原来的 key->hash
// % capacity 会发生变化，导致结果不准确。
static void adjustCapacity(Table *table, int capacity) {
  Entry *entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  table->count = 0;
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key == NULL) {
      continue;
    }

    // 重新分配
    Entry *dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

// 像 hash 表添加元素
bool tableSet(Table *table, ObjString *key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(table, capacity);
  }

  Entry *entry = findEntry(table->entries, table->capacity, key);

  bool isNewKey = entry->key == NULL;
  if (isNewKey && IS_NIL(entry->value))
    table->count++;  // 墓碑也被 count 计数了，
  // 所以当存在很多墓碑时可能会过早的增加 table

  entry->key = key;
  entry->value = value;
  return isNewKey;
}

bool tableDelete(Table *table, ObjString *key) {
  if (table->count == 0) return false;

  Entry *entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  entry->key = NULL;
  entry->value = BOOL_VAL(true);

  return true;
}

void tableAddAll(Table *from, Table *to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry *entry = &from->entries[i];
    if (entry->key != NULL) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString *tableFindString(Table *table, const char *chars, int length,
                           uint32_t hash) {
  if (table->count == 0) return NULL;

  uint32_t index = hash % table->capacity;

  for (;;) {
    Entry *entry = &table->entries[index];

    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) return NULL;
    } else if (entry->key->length == length && entry->key->hash == hash &&
        memcmp(entry->key->chars, chars, length) == 0) {
      // memcmp 比较内存的前 n 个字节， 若两个字符串完全相同则返回 0，否则返回最后一个不为0的字符吃 ascii 码差值。
      return entry->key;
    }

    // 这里再次取模可以避免 index 溢出
    index = (index + 1) % table->capacity;
  }
}

void markTable(Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    markObject((Obj *) entry->key);
    markValue(entry->value);
  }
}

// 由于 table entries hash 表"单独"指向了 object string (为了实现 intern string 功能)
//  但是如果该 obj string 没有被任何其他 root 引用，那么它将被清除
// 此时 entry->key->obj 将是一个悬空指针
// 因此需要在 obj string 被清除之前，将其从 hash 表引用中删除，避免悬空指针。
void tableRemoveWhite(Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    // entry 是一个 hash 表。
    Entry *entry = &table->entries[i];
    // 没被标记？
    if (entry->key != NULL && !entry->key->obj.isMarked) {
      tableDelete(table, entry->key);
    }
  }
}
