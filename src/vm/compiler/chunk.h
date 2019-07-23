#ifndef blu_chunk_h
#define blu_chunk_h

#include "util/buffer.h"
#include "vm/compiler/opcode.h"
#include "vm/value.h"

DECLARE_BUFFER(bluValue, bluValue);

typedef struct {
	const char* file;
	const char* name;

	ByteBuffer code;
	IntBuffer lines;
	IntBuffer columns;

	bluValueBuffer constants;
} bluChunk;

void bluChunkInit(bluChunk* chunk);
void bluChunkFree(bluChunk* chunk);

void bluChunkWrite(bluChunk* chunk, uint8_t byte, int32_t line, int32_t column);

#endif
