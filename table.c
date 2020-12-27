#include "table.h"

#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  // key->hash 为已经计算并缓存好的分布均匀的 hash 值，不需要重复计算。
  // 通过 hash 值 % capacity 使其分布到具体的位置。
  uint32_t index = key->hash % capacity;
  for (;;) { // 负载系数使得死循环的情况不会发生。
    Entry* entry = &entries[index];
    // 如果对应的桶的 key 和寻找的 key 一致则返回
    // 如果 entry->key == key 表示 key 存储在桶中，可以返回
    // 如果 entry->key == NULL 表示这是一个空桶，找到空桶表示该 key 并没有存储在桶中
    if (entry->key == key || entry->key == NULL) { 
      return entry;
    }

    // 继续去下一个桶，知道找到对应的值
    index = (index + 1) % capacity;
  }
}

// 需要注意 hash 表调整大小时，如果原来的 hash 表中存在 key， 则原来的 key->hash % capacity 会发生变化，导致结果不准确。
static void adjustCapacity(Table* table, int capacity){
  Entry* entries = ALLOCATE(Entry, capacity); 
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  table->entries = entries; // 这不就覆盖了？？？
  table->capacity = capacity;
}

bool tableSet(Table* table, ObjString* key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(table, capacity);
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);

  bool isNewKey = entry->key == NULL;
  if (isNewKey) table->count++;

  entry->key = key;
  entry->value = value;
  return isNewKey;
}