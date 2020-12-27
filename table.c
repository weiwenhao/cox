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

static Entry *findEntry(Entry *entries, int capacity, ObjString *key) {
  // key->hash 为已经计算并缓存好的分布均匀的 hash 值，不需要重复计算。
  // 通过 hash 值 % capacity 使其分布到具体的位置。
  uint32_t index = key->hash % capacity;

  Entry *tombstone = NULL;

  for (;;) { // 负载系数使得死循环的情况不会发生。
    Entry *entry = &entries[index];
    // 可能是被删除的值或者是压根就没有存过值在这里
    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        // 当找到一个真正空的 entry 时,此时可以确实 table 中不存在我想找到的 hash 值
        // 那么可以推断出外界可以插入，此时如果查找到真空 entry 之前遇到过墓碑节点
        // 则把墓碑节点返回给外界使用
        return tombstone != NULL ? tombstone : entry;
      } else {
        if (tombstone == NULL) tombstone = entry; // 找到墓碑并不停止
      }
    } else if (entry->key == key) {
      return entry;
    }

    // 继续去下一个桶，知道找到对应的值
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

// 需要注意 hash 表调整大小时，如果原来的 hash 表中存在 key， 则原来的 key->hash % capacity 会发生变化，导致结果不准确。
static void adjustCapacity(Table *table, int capacity) {
  Entry *entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key == NULL) {
      continue;
    }

    // 重新分配
    Entry *dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
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
  if (isNewKey) table->count++;

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