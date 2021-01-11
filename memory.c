

#include "memory.h"

#include <stdlib.h>

#include "common.h"
#include "vm.h"

void *reallocate(void *previous, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(previous);
    return NULL;
  }

  // previous 是不是已经能够获取 size 了， 不需要任何多余的操作
  return realloc(previous, newSize);  // 关键就是这里了
}

static void freeObject(Obj *object) {
  switch (object->type) {
    case OBJ_CLOSURE: {
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction *function = (ObjFunction *) object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_NATIVE:FREE(ObjNative, object);
      break;
    case OBJ_STRING: {
      ObjString *string = (ObjString *) object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
  }
}

void freeObjects() {
  Obj *object = vm.objects;
  while (object != NULL) {
    Obj *next = object->next;
    freeObject(object);
    object = next;
  }
}