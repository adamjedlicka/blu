#ifndef blu_compiler_h
#define blu_compiler_h

#include "chunk.h"
#include "opcode.h"
#include "util/buffer.h"
#include "vm/parser/parser.h"

DECLARE_BUFFER(bluOpCode, bluOpCode);

typedef struct {
	bluParser parser;
	bluOpCodeBuffer opCodeBuffer;

	bluToken previous;
	bluToken current;

	bluChunk chunk;

	bool hadError;
	bool panicMode;
} bluCompiler;

void bluCompilerInit(bluCompiler* compiler, const char* source);
void bluCompilerFree(bluCompiler* compiler);

void bluCompilerCompile(bluCompiler* compiler);

#endif
