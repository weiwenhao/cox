#ifndef COX__DEBUG_H_
#define COX__DEBUG_H_

#include "chunk.h"

void disassembleChunk(Chunk *chunk, const char *name);
int disassembleInstruction(Chunk *chunk, int offset);

static int simpleInstruction(const char *name, int offset);
static int constantInstruction(const char *name, Chunk *chunk, int offset);
static int byteInstruction(const char *name, Chunk *chunk, int offset);

#endif //COX__DEBUG_H_
