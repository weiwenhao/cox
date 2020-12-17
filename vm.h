#ifndef COX__VM_H_
#define COX__VM_H_

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;
  uint8_t* ip;  // ip 指向当前正在执行的指令
  Value stack[STACK_MAX];
  Value* stackTop;
  Obj* objects;  // 垃圾回收的起点
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif  // COX__VM_H_
