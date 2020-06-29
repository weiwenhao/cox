#include <stdlib.h>

#include "common.h"
#include "memory.h"

void* reallocate(void* previous, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(previous);
    return NULL;
  }

  // previous 是不是已经能够获取 size 了， 不需要任何多余的操作
  return realloc(previous, newSize); // 关键就是这里了
}