#ifndef blu_compiler_h
#define blu_compiler_h

#include "blu.h"
#include "chunk.h"
#include "vm/common.h"
#include "vm/object.h"
#include "vm/parser/parser.h"

typedef struct {
	bluToken name;
	int8_t depth;

	// True if this local variable is captured as an upvalue by a function.
	bool isUpvalue;
} bluLocal;

DECLARE_BUFFER(bluLocal, bluLocal);

typedef struct {
	// The index of the local variable or upvalue being captured from the enclosing function.
	uint16_t index;

	// Whether the captured variable is a local or upvalue in the enclosing function.
	bool isLocal;
} bluUpvalue;

DECLARE_BUFFER(bluUpvalue, bluUpvalue);

typedef enum {
	TYPE_TOP_LEVEL,
	TYPE_FUNCTION,
} bluFunctionType;

typedef struct bluCompiler {
	bluVM* vm;
	bluParser* parser;
	struct bluCompiler* enclosing;

	bluObjFunction* function;
	bluFunctionType type;

	bluLocalBuffer locals;
	bluUpvalueBuffer upvalues;

	int8_t scopeDepth;

	bool hadError;
	bool panicMode;
} bluCompiler;

bluObjFunction* bluCompilerCompile(bluVM* vm, bluCompiler* compiler, const char* source, const char* name);

#endif
