#ifndef blu_debug_h
#define blu_debug_h

#include "vm/compiler/chunk.h"

int32_t bluDisassembleInstruction(bluChunk* chunk, int32_t offset);
void bluDisassembleChunk(bluChunk* chunk);

#endif
