#ifndef blu_compiler_h
#define blu_compiler_h

#include "chunk.h"
#include "vm/parser/parser.h"

typedef struct {
	bluParser parser;

	bluToken previous;
	bluToken current;

	bluChunk* chunk;

	bool hadError;
	bool panicMode;
} bluCompiler;

void bluCompilerInit(bluCompiler* compiler, const char* source);
void bluCompilerFree(bluCompiler* compiler);

bool bluCompilerCompile(bluCompiler* compiler, bluChunk* chunk);

#endif
