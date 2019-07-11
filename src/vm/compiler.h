#ifndef blu_compiler_h
#define blu_compiler_h

#include "blu.h"
#include "object.h"
#include "scanner.h"

typedef struct {
	bluToken current;
	bluToken previous;
	bool hadError;
	bool panicMode;
} bluParser;

typedef struct {
	bluToken name;
	int depth;

	// True if this local variable is captured as an upvalue by a function.
	bool isUpvalue;
} bluLocal;

typedef struct {
	// The index of the local variable or upvalue being captured from the enclosing function.
	uint8_t index;

	// Whether the captured variable is a local or upvalue in the enclosing function.
	bool isLocal;
} bluUpvalue;

typedef enum {
	TYPE_ANONYMOUS,
	TYPE_FUNCTION,
	TYPE_INITIALIZER,
	TYPE_TOP_LEVEL,
	TYPE_METHOD,
} bluFunctionType;

typedef struct bluClassCompiler {
	struct bluClassCompiler* enclosing;

	bluToken name;
	bool hasSuperclass;
} bluClassCompiler;

typedef struct bluCompiler {
	bluScanner* scanner;
	bluParser* parser;
	bluVM* vm;

	// The compiler for the enclosing function, if any.
	struct bluCompiler* enclosing;

	bluClassCompiler* classCompiler;

	// The function being compiled.
	bluObjFunction* function;
	bluFunctionType type;

	bluLocal locals[UINT8_COUNT];
	int localCount;
	bluUpvalue upvalues[UINT8_COUNT];
	int scopeDepth;

	bool isPrivate;
	bool inLoop;
	int currentBreak;
} bluCompiler;

void bluInitCompiler(bluVM* vm, bluCompiler* compiler, bluScanner* scanner, bluParser* parser, bluCompiler* current,
					 int scopeDepth, bluFunctionType type);
void bluInitParser(bluParser* parser);

bluObjFunction* bluCompile(bluVM* vm, const char* source);

void bluGrayCompilerRoots(bluVM* vm);

#endif
