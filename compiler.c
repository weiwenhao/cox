#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scanner.h"

#ifdef DEBUG_PRINT_CODE

#include "debug.h"

#endif

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int depth; // 变量所处的 scope 深度，和 scopeDepth 是一个概念！！！
} Local;

typedef struct {
  Local locals[UINT8_COUNT];
  int localCount; // 变量数量
  int scopeDepth; // 深度
} Compiler;

Parser parser;

Compiler *current = NULL;

Chunk *compilingChunk;

static uint8_t identifierConstant(Token *name);
static int resolveLocal(Compiler *compiler, Token *name);

static Chunk *currentChunk() { return compilingChunk; }

static void errorAt(Token *token, const char *message) {
  if (parser.panicMode) return;  // 不需要重复异常处理
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char *message) { errorAt(&parser.previous, message); }

static void errorAtCurrent(const char *message) {
  errorAt(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

// 返回需要添加补丁的chunk 部分。
static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  // 一个字节只能存储 8 位二进制，则最大只能偏移 255 个指令,这显然是不够的
  // 因此使用 2 个字节，表示 65536 个偏移指令
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void emitReturn() { emitByte(OP_RETURN); }

static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t) constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset) {
  // - 2 to adjust for the bytecode for the jump offset itself
  // - offset 计算出偏移量（且预留 offset 位置）
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");

    // jump 表示当前指令的 index，即绝对位置
    // 并将该绝对位置，存放在两个字节中
    // 第一个字节存储前 8 位二进制
    // 第二个字节存储后 8 位二进制
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
  }
}

static void initCompiler(Compiler *compiler) {
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  current = compiler;
}

static void endCompiler() {
  emitReturn();
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
#endif
}

static void beginScope() {
  current->scopeDepth++;
}

static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
      current->locals[current->localCount - 1].depth > current->scopeDepth) {
    emitByte(OP_POP);
    current->localCount--;
  }
}

// 预先声明
static void expression();
static void statement();  // 递归操作，所以需要提前声明
static void declaration();

static ParseRule *getRule(TokenType type);

static void parsePrecedence(Precedence precedence);

static void binary(bool canAssign) {
  // Remember the operator.
  TokenType operatorType = parser.previous.type;

  // Compile the right operand. (解析表达式右边部分, 把中缀改成前缀)
  // get rule 获取当前运算符的优先级
  ParseRule *rule = getRule(operatorType);
  parsePrecedence((Precedence) (rule->precedence + 1));

  // Emit the operator instruction.
  switch (operatorType) {
    case TOKEN_BANG_EQUAL:emitBytes(OP_EQUAL, OP_NOT);
      break;
    case TOKEN_EQUAL_EQUAL:emitByte(OP_EQUAL);
      break;
    case TOKEN_GREATER:emitByte(OP_GREATER);
      break;
    case TOKEN_GREATER_EQUAL:emitBytes(OP_LESS, OP_NOT);
      break;
    case TOKEN_LESS:emitByte(OP_LESS);
      break;
    case TOKEN_LESS_EQUAL:emitBytes(OP_GREATER, OP_NOT);
      break;
    case TOKEN_PLUS:emitByte(OP_ADD);
      break;
    case TOKEN_MINUS:emitByte(OP_SUBTRACT);
      break;
    case TOKEN_STAR:emitByte(OP_MULTIPLY);
      break;
    case TOKEN_SLASH:emitByte(OP_DIVIDE);
      break;
    default:return;  // Unreachable.
  }
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE:emitByte(OP_FALSE);
      break;
    case TOKEN_NIL:emitByte(OP_NIL);
      break;
    case TOKEN_TRUE:emitByte(OP_TRUE);
      break;
    default:return;  // Unreachable.
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
  // 字符串转 double
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
  emitConstant(OBJ_VAL(
                   copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign) {
  // 所有的变量名称在底层应该具有相同的地址，这样可以方便比较，和 hash 表查找
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else {
    // 常量化字符串
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    // 修改变量
    expression(); // 写入计算结果在栈中
    emitBytes(setOp, arg);
  } else {
    emitBytes(getOp, arg);
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  // Compile the operand.
  // 如果运算符的优先级比 -xx 高，则优先求值, 如 - (a - 3) + 1
  parsePrecedence(PREC_UNARY);

  // Emit the operator instruction.
  switch (operatorType) {
    case TOKEN_BANG:emitByte(OP_NOT);
      break;
    case TOKEN_MINUS:emitByte(OP_NEGATE);
      break;
    default:return;  // Unreachable.
  }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, NULL, PREC_NONE},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, NULL, PREC_NONE},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
  advance();
  // number 本身也是前缀表达式，因此这里肯定会有一个前缀会被解析
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }
  bool canAssign = precedence <= PREC_ASSIGNMENT;

  prefixRule(canAssign);  // 判断是否有前缀表达式

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  // 如果需要赋值，但是确没有赋值这是一个错误，需要报告
  // 如 a * b = 1 + 3;
  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static uint8_t identifierConstant(Token *name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

// 返回的 i 是变量在 locals 中的索引，有什么意义吗？
// 编译完成后 locals 还一直存在？？？？
// 疑问： i 和 vm.stack 对应吗？？？？
// vm.stack 中不是还有其他指令，比如 POP 等占用吗？
static int resolveLocal(Compiler *compiler, Token *name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local *local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local *local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1; // 声明但未初始化的特殊标志
}

static void declareVariable() {
  if (current->scopeDepth == 0) return;

  Token *name = &parser.previous;
  for (int i = current->localCount - 1; i >= 0; i--) {
    // locals 包含所有的作用域！！
    Local *local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

static uint8_t parseVariable(const char *errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0) return 0;

  return identifierConstant(&parser.previous);
}

static void markInitialized() {
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

// 定义全局变量
static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    // 虽然你不敢相信，但是局部变量现在已经创建好了，其已经放在了栈顶！！！
    // 表达式是从右往左计算并编译的
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

static ParseRule *getRule(TokenType type) { return &rules[type]; }

static void expression() { parsePrecedence(PREC_ASSIGNMENT); }

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression(); // 表达式会产生一个值，该值会写入到栈顶
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  // 计算当 if 表达式为 false 时，IP 需要指向的指令序列。
  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  // 如果进入到该条件，则弹出 expresion 写入的栈值
  // 如果么有进入，则需要交给后面的 jump 后弹出
  emitByte(OP_POP);
  statement(); // 编译 if 表达式的 body 部分。

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitJump(OP_POP);

  if (match(TOKEN_ELSE)) statement();
  patchJump(elseJump);
}

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void varDeclaration() {
  // 并不会真的存储全局变量的名称，而是将全局变量的名称添加到常量表中
  // 然后使用常量表的 index 索引来定义全局变量
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
  defineVariable(global);
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  // 关键关键关键！
  // 非表达式直接弹出!!! 防止堆在栈里面
  emitByte(OP_POP);
}

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;

    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:return;

      default:
        // Do nothing.
        ;
    }

    advance();
  }
}

static void declaration() {
  if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) synchronize();
}

static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

bool compile(const char *source, Chunk *chunk) {
  initScanner(source);

  Compiler compiler;
  initCompiler(&compiler);

  compilingChunk = chunk;

  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }
  endCompiler();

  return !parser.hadError;
}
