#include "vm.h"

#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"

VM vm;  // 全局变量，用于数据共享

static Value clockNative(int argCount, Value *args) {
  return NUMBER_VAL((double) clock() / CLOCKS_PER_SEC);
}

static Value peek(int distance);
static bool isFalsey(Value value);
static void closeUpvalues(Value *last);
static void concatenate();

static void resetStack() {
  vm.stackTop = vm.stack;  // 变量名是一个指针，指向数组的开始位置
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static void runtimeError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame *frame = &vm.frames[i];
    ObjFunction *function = frame->closure->function;
    // -1 because the IP is sitting on the next instruction to be
    // executed.
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ",
            function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }
  resetStack();
}

static void defineNative(const char *name, NativeFn function) {
  // 避免被垃圾收集释放？？
  push(OBJ_VAL(copyString(name, (int) strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

static InterpretResult run() {
  CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++) // ip 指向 chunk!  stackTop 执行 stack!!!
#define READ_SHORT() \
    (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
// OP_CONSTANT 的下一条指定总是 constant 对应的索引
// 因为 compiler emit 时就是这么安排的
// 所以获取常量的值的时候，直接读取下一条指令(字节码)即可
// 不止是常量，所有的编译字节码都遵循这个原则
#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

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
    disassembleInstruction(&frame->closure->function->chunk,
                           (int) (frame->ip - frame->closure->function->chunk.code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);  // push 进去干啥？ 有啥用？
//        printValue(constant);
//        printf("\n");
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
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        // 这里 tm 是指针偏移操作， 由于使用同一个 stack， 所以一切都可以实现！
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString *name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        // 为什么可以直接读出来常量名称？最顶部的就是常量名称？
        // 常量名称现在是数字还是 index 索引？？
        // ？？？
        ObjString *name = READ_STRING();
        // peek 和 pop 的唯一差别就是，peek 不弹出值
        // 指令以及名称被读取后，剩下的则是变量的 value
        // 此处 peek 获取的是变量的值！！！
        // 并将变量的值放在 hash 表中
        tableSet(&vm.globals, name, peek(0));
        pop();
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString *name = READ_STRING();
        // 如果 table set 返回 true，表示 name 是一个新的值
        // 则说明全局变量为定义，何谈修改一说
        bool isNewKey = tableSet(&vm.globals, name, peek(0));
        if (isNewKey) {
          tableDelete(&vm.globals, name);
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
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
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE(); // 从指令中获取参数数量
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CLOSURE: {
        // 编译 OP_CLOSURE 顺便解析一下 upvalue 在栈中的绝对位置
        ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure *closure = newClosure(function); // ?? 运行时操作？?
        push(OBJ_VAL(closure));

        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE(); // 这里存储的是相对位置
          if (isLocal) {
            // 计算绝对位置
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }

        break;
      }
      case OP_CLOSE_UPVALUE:closeUpvalues(vm.stackTop - 1);
        pop();
        break;
      case OP_RETURN: {
        Value result = pop(); // 弹出 返回值

        closeUpvalues(frame->slots); // up value in heap

        // 打消 init 时的 vm.frameCount++,使 frameCount 指向当前调用栈
        // 下面的 vm.frameCount -1 则是返回到上一个调用栈。
        vm.frameCount--;
        if (vm.frameCount == 0) {
          pop(); //  弹出 script function point
          return INTERPRET_OK; // 退出 run 函数
        }

        // 这里相当于丢弃了 slots 右边的所有临时变量
        vm.stackTop = frame->slots;
        // 然后将函数返回值重新丢进堆栈中
        push(result);

        frame = &vm.frames[vm.frameCount - 1];
        // 中断后续 switch 判断，进入下一次 for 指令循环
        break;
      }
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

void initVM() {
  resetStack();
  vm.objects = NULL;
  initTable(&vm.globals);
  initTable(&vm.strings);

  defineNative("clock", clockNative);
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
  Value value = vm.stackTop[-1 - distance];
  return value;
}

// TODO
static bool call(ObjClosure *closure, int argCount) {
  // 参数数量不匹配检测
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments bug got %d.", closure->function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  // 向下一层，并在当前层保存下一层的 closure
  CallFrame *frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;

  return true;
}

// ???
static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLOSURE:return call(AS_CLOSURE(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        Value result = native(argCount, vm.stackTop - argCount);
        vm.stackTop -= argCount + 1; // 手动丢弃临时变量参数列表
        push(result); // 保存函数返回结果
        return true;
      }
      default:break;
    }
  }

  runtimeError("Can only call function and classes.");
  return false;
}

// 如果两个闭包捕获同一个变量，则他们拥有相同的 upvalue
static ObjUpvalue *captureUpvalue(Value *local) {
  ObjUpvalue *prevUpvalue = NULL;
  ObjUpvalue *upvalue = vm.openUpvalues;

  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue *createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;

  // 连表插入
  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(Value *last) {
  // 为什么关闭一个变量要捕获多个值？？？？？？
  // 难道不是只有一个吗
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;
    // 获取 location 指向的值存入到 closed
    // 并将 location 从新指向自身，从而封闭整个 upvalue
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
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
  ObjFunction *function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));

  // 执行 top function
  ObjClosure *closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  // 函数调用
  callValue(OBJ_VAL(closure), 0);

  return run();
}
