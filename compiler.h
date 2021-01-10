#ifndef COX__COMPILER_H_
#define COX__COMPILER_H_

#include "chunk.h"
#include "object.h"

ObjFunction *compile(const char *source);
static uint8_t argumentList();

#endif //COX__COMPILER_H_
