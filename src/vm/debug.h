#ifndef blu_debug_h
#define blu_debug_h

#include "chunk.h"

void bluDisassembleChunk(bluVM* vm, bluChunk* chunk, const char* name);

int bluDisassembleInstruction(bluVM* vm, bluChunk* chunk, int offset);

#endif
