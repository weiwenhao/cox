#ifndef COX__VM_H_
#define COX__VM_H_

#include "chunk.h"
#include "table.h"
#include "value.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  ObjClosure *closure; // TODO 何解？？？
  uint8_t *ip;
  Value *slots; // 相当于函数内部栈指针！！！ 也就是 c 语言的 EBP 寄存器 !!!
} CallFrame;

typedef struct {
//  Chunk *chunk;
//  uint8_t *ip;  // ip 指向当前正在执行的指令
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value stack[STACK_MAX];
  Value *stackTop; // 支持，恒定指向栈顶
  Table globals;
  Table strings;  // 存储所有的字符串表
  ObjUpvalue *openUpvalues;

  size_t bytesAllocated;
  size_t nextGC;

  Obj *objects; // 垃圾回收的起点？？
  int grayCount; // 实际数量
  int grayCapacity; // 栈的总容量空间
  Obj **grayStack; // 灰色节点缓存

} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char *source);
void push(Value value);
Value pop();
static bool callValue(Value callee, int argCount);
static ObjUpvalue *captureUpvalue(Value *local);

#endif  // COX__VM_H_
