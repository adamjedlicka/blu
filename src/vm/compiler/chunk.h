#ifndef blu_chunk_h
#define blu_chunk_h

#include "util/buffer.h"
#include "vm/compiler/opcode.h"
#include "vm/value/value.h"

DECLARE_BUFFER(bluValue, bluValue);

typedef struct {
	const char* name;

	ByteBuffer code;
	IntBuffer lines;
	IntBuffer columns;

	bluValueBuffer constants;
} bluChunk;

void bluChunkInit(bluChunk* chunk, const char* name);
void bluChunkFree(bluChunk* chunk);

void bluChunkWrite(bluChunk* chunk, uint8_t byte, uint32_t line, uint32_t column);

#endif
