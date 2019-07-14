#ifndef blu_debug_h
#define blu_debug_h

#include "vm/compiler/chunk.h"

uint32_t bluDisassembleInstruction(bluChunk* chunk, size_t offset);
void bluDisassembleChunk(bluChunk* chunk, const char* name);

#endif
