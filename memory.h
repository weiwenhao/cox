#ifndef COX__MEMORY_H_
#define COX__MEMORY_H_

#include "object.h"

#define GROW_CAPACITY(capacity) \
  ((capacity) < 8 ? 8 : (capacity)*2)  // 这又是什么神奇的表达式？

#define ALLOCATE(type, count) (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

// （type*) 把申请的内存空间定义为 utf8_t 类型,
// 其实这一段空间已经是一块固定长度的数组咯
#define GROW_ARRAY(previous, type, oldCount, count) \
  (type*)reallocate(previous, sizeof(type) * (oldCount), sizeof(type) * (count))

#define FREE_ARRAY(type, pointer, oldCount) \
  reallocate(pointer, sizeof(type) * (oldCount), 0)

void *reallocate(void *previous, size_t oldSize, size_t newSize);
void markObject(Obj *object);
void markValue(Value value);
void collectGarbage();
void freeObjects();

#endif  // COX__MEMORY_H_
