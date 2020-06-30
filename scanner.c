#include "stdio.h"
#include "string.h"

#include "scanner.h"
#include "common.h"

typedef struct {
  const char *start;
  const char *current;
  int line;
} Scanner;

Scanner scanner; // 这算是一个全局变量

void initScanner(const char *source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

static bool isAtEnd() {
  return *scanner.current == '\0';
}

static char advance() {
  scanner.current++;
  return scanner.current[-1];
}

static char peekNext() {
  if (isAtEnd()) return '\0';
  return scanner.current[1]; // current 指针的下一个指针位置的值, 不移动 current 指针
}

static char peek() {
  return *scanner.current;
}

static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;

  scanner.current++;
  return true;
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int) (scanner.current - scanner.start);
  token.line = scanner.line;

  return token;
}

static Token errorToken(const char *message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int) strlen(message);
  token.line = scanner.line;

  return token;
}

static void skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':advance();
        break;
      case '\n':scanner.line++;
        advance(); // 如果当前字符是空白符则 advance scanner.current . 放弃返回值，即丢弃空白符
        break;
      case '/': // 跳过注释
        if (peekNext() == '/') {
          // 直到遇到换行符，但是不丢弃换行符， 换行符会在下一轮 skipWhitespace 中被识别，并使得 scanner.line 递增
          while (peek() != '\n' && !isAtEnd()) {
            advance();
          }
        } else {
          return; // 不丢弃第一个 /
        }
      default:return;
    }
  }
}

Token scanToken() {
  skipWhitespace();

  scanner.start = scanner.current;

  if (isAtEnd()) {
    return makeToken(TOKEN_EOF);
  };

  char c = advance();

  switch (c) {
    // 单字符处理
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': return makeToken(TOKEN_LEFT_BRACE);
    case '}': return makeToken(TOKEN_RIGHT_BRACE);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': return makeToken(TOKEN_DOT);
    case '-': return makeToken(TOKEN_MINUS);
    case '+': return makeToken(TOKEN_PLUS);
    case '/': return makeToken(TOKEN_SLASH);
    case '*': return makeToken(TOKEN_STAR);
      // 多字符处理
    case '!':return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
  }

  return errorToken("Unexpected character.");
}
