#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"

VM vm;  // 全局变量，用于数据共享

static Value peek(int distance);
static bool isFalsey(Value value);
static void concatenate();

static void resetStack() {
  vm.stackTop = vm.stack;  // 变量名是一个指针，指向数组的开始位置
}

static void runtimeError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  size_t instruction = vm.ip - vm.chunk->code - 1;
  int line = vm.chunk->lines[instruction];
  fprintf(stderr, "[line %d] in script\n", line);

  resetStack();
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++) // 读取当前指令？
// OP_CONSTANT 的下一条指定总是 constant 对应的索引
// 因为 compiler emit 时就是这么安排的
// 所以获取常量的值的时候，直接读取下一条指令(字节码)即可
// 不止是常量，所有的编译字节码都遵循这个原则
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op)                      \
  do {                                                \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
      runtimeError("Operands must be numbers.");      \
      return INTERPRET_RUNTIME_ERROR;                 \
    }                                                 \
    double b = AS_NUMBER(pop());                      \
    double a = AS_NUMBER(pop());                      \
    push(valueType(a op b));                          \
  } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(vm.chunk, (int) (vm.ip - vm.chunk->code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);  // push 进去干啥？ 有啥用？
        printValue(constant);
        printf("\n");
        break;
      }
      case OP_NIL:push(NIL_VAL);
        break;
      case OP_TRUE:push(BOOL_VAL(true));
        break;
      case OP_FALSE:push(BOOL_VAL(false));
        break;
      case OP_POP:pop();
        break;
      case OP_DEFINE_GLOBAL: {
        // 为什么可以直接读出来常量名称？最顶部的就是常量名称？
        // 常量名称现在是数字还是 index 索引？？
        // ？？？
        ObjString *name = READ_STRING();
        // peek 和 pop 的唯一差别就是，peek 不弹出值
        tableSet(&vm.globals, name, peek(0));
        pop();
        break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER:BINARY_OP(BOOL_VAL, >);
        break;
      case OP_LESS:BINARY_OP(BOOL_VAL, <);
        break;
      case OP_ADD:
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or tow strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      case OP_SUBTRACT:BINARY_OP(NUMBER_VAL, -);
        break;
      case OP_MULTIPLY:BINARY_OP(NUMBER_VAL, *);
        break;
      case OP_DIVIDE:BINARY_OP(NUMBER_VAL, /);
        break;
      case OP_NOT:push(BOOL_VAL(isFalsey(pop())));
        break;
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      case OP_PRINT: {
        // when the interpreter reaches this instruction, it has already
        // executed the code for the expression leaving the result value on top
        // of the stack
        printValue(pop());
        printf("\n");
        break;
      }
      case OP_RETURN: {
        // printValue(pop());
        // printf("\n");
        return INTERPRET_OK;
      }
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

void initVM() {
  resetStack();
  vm.objects = NULL;
  initTable(&vm.globals);
  initTable(&vm.strings);
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  freeObjects();
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

// 返回栈中的元素,但不弹出栈
static Value peek(int distance) {
  // 获取指针前一个 1 - distance 的值
  // 等价于访问数组中的元素, vm.stackTop 是一个动态支持，默认指向数组的最后一个元素，所以可以使用 -1 作为下标
  // 直接使用数组名称访问时，数组名称为常量，默认执行数组的第 0 个元素。
  // 如果直接使用数组名[-1] 则会造成数组越界问题
  return vm.stackTop[-1 - distance];
}

// 只有 nil 和 false 为 false,其余值都为 true
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString *b = AS_STRING(pop());
  ObjString *a = AS_STRING(pop());

  int length = a->length + b->length;
  char *chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString *result = takeString(chars, length);
  push(OBJ_VAL(result));
}

InterpretResult interpret(const char *source) {
  Chunk chunk;
  initChunk(&chunk);

  if (!compile(source, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_COMPILE_ERROR;
  }

  vm.chunk = &chunk;
  vm.ip = vm.chunk->code;

  InterpretResult result = run();
  freeChunk(&chunk);

  return result;
}
