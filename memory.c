#include "memory.h"

#include <stdlib.h>

#include "compiler.h"
#include "common.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

void *reallocate(void *previous, size_t oldSize, size_t newSize) {
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif
  }

  if (newSize == 0) {
    free(previous);
    return NULL;
  }

  // previous 是不是已经能够获取 size 了， 不需要任何多余的操作
  return realloc(previous, newSize);  // 关键就是这里了
}

void markValue(Value value) {
  if (!IS_OBJ(value)) return; // 只有 obj 才能引用堆栈
  markObject(AS_OBJ(value));
}

static void markArray(ValueArray *array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}

static void blackenObject(Obj *object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void *) object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure *closure = (ObjClosure *) object;
      markObject((Obj *) closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject((Obj *) closure->upvalues[i]);
      }
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction *function = (ObjFunction *) object;
      markObject((Obj *) function->name);
      markArray(&function->chunk.constants);
      break;
    }
    case OBJ_UPVALUE:markValue(((ObjUpvalue *) object)->closed);
      break;
      // 本地函数和字符串么有其他引用，所以没什么可以遍历的
    case OBJ_NATIVE:
    case OBJ_STRING:break;
  }
}

static void freeObject(Obj *object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void *) object, object->type);
#endif

  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure *closure = (ObjClosure *) object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
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
    case OBJ_UPVALUE:FREE(ObjUpvalue, object);
      break;;
  }
}

void freeObjects() {
  Obj *object = vm.objects;
  while (object != NULL) {
    Obj *next = object->next;
    freeObject(object);
    object = next;
  }

  free(vm.grayStack);
}

// 标记阶段发生在运行阶段
static void markRoots() {
  // 标记整个栈空间
  for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  // 标记所有闭包
  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj *) vm.frames[i].closure);
  }

  // upvalue 链表结构
  for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
    markObject((Obj *) upvalue);
  }

  markTable(&vm.globals);

  markCompilerRoots();
}

static void traceReferences() {
  while (vm.grayCount > 0) {
    Obj *object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

static void sweep() {
  Obj *previous = NULL;
  Obj *object = vm.objects;
  while (object != NULL) {
    if (object->isMarked) {
      object->isMarked = false;
      previous = object;
      object = object->next; // 重新排列
    } else {
      Obj *unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
#endif

  markRoots();
  traceReferences();
  tableRemoveWhite(&vm.strings);
  sweep();

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
#endif
}

void markObject(Obj *object) {
  if (object == NULL) return;
  if (object->isMarked) return;
#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void *) object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  object->isMarked = true;

  // 如果栈申请的空间满了，就再申请呗
  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = realloc(vm.grayStack, sizeof(Obj *) * vm.grayCapacity);

    if (vm.grayStack == NULL) exit(1);
  }

  // 广度优先算法需要一个栈来协助遍历，栈就是一个工作列表
  vm.grayStack[vm.grayCount++] = object;
}
