#ifndef blu_chunk_h
#define blu_chunk_h

#include "common.h"
#include "util/buffer.h"
#include "vm/compiler/opcode.h"
#include "vm/value/value.h"

DECLARE_BUFFER(bluValue, bluValue);

typedef struct {
	ByteBuffer code;
	bluValueBuffer constants;
} bluChunk;

void bluChunkInit(bluChunk* chunk);
void bluChunkFree(bluChunk* chunk);

void bluChunkWrite(bluChunk* chunk, uint8_t byte, uint32_t line);

#endif
