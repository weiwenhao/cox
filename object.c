#include "object.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
  (type*)allocateObject(sizeof(type), objectType)

static Obj *allocateObject(size_t size, ObjType type) {
  Obj *object = (Obj *) reallocate(NULL, 0, size);
  object->type = type;
  object->next = vm.objects;
  vm.objects = object;
  return object;
}

ObjClosure *newClosure(ObjFunction *function) {
  ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjFunction *newFunction() {
  ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);

  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);

  return function;
}

ObjNative *newNative(NativeFn function) {
  ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;
  return native;
}

// 所有分配的字符串都需要在 hash 表中存储一份从而避免重复创建
static ObjString *allocateString(char *chars, int length, uint32_t hash) {
  ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  // string 为 key, value 为 nil
  tableSet(&vm.strings, string, NIL_VAL);

  return string;
}

// fnv-la hash
// length 表示需要计算 hash 的字符串的长度, hash
// 值会被均匀的分布在一个很大的数字范围内
static uint32_t hashString(const char *key, int length) {
  // 最后的 u 是什么意思
  uint32_t hash = 2166136261u;

  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }

  return hash;
}

// 拼接完成字符串后，使用一次该方法检测拼接后的字符串是否是 inter string, 如果是则释放拼接后的字符串，并返回 inter string
ObjString *takeString(char *chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }
  return allocateString(chars, length, hash);
}

ObjString *copyString(const char *chars, int length) {
  // 计算 hash 值并缓存
  uint32_t hash = hashString(chars, length);

  // 这里不能直接调用 tableGet 因为其调用的 findEntry 中使用了 == 进行 key 的比较
  ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
  // 此事 chars 还没有创建并分配内存空间，所以不存在释放一说
  if (interned != NULL) {
    return interned;
  }

  // 此处才为 string 分配内存红箭在 heap 中, 并将对应的指针分配给 value
  char *heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  return allocateString(heapChars, length, hash);
}

// slot 指向栈空间
ObjUpvalue *newUpvalue(Value *slot) {
  ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}

static void printFunction(ObjFunction *function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_CLOSURE:printFunction(AS_CLOSURE(value)->function);
      break;
    case OBJ_FUNCTION:printFunction(AS_FUNCTION(value));
      break;
    case OBJ_NATIVE:printf("<native fun>");
      break;
    case OBJ_STRING:printf("%s", AS_CSTRING(value));
      break;
    case OBJ_UPVALUE: printf("upvalue");
      break;
  }
}