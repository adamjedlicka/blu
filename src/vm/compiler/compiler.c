#include "compiler.h"
#include "blu.h"
#include "vm/debug/debug.h"
#include "vm/memory.h"
#include "vm/object.h"
#include "vm/value.h"
#include "vm/vm.h"

DEFINE_BUFFER(bluLocal, bluLocal);
DEFINE_BUFFER(bluUpvalue, bluUpvalue);

typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT, // =
	PREC_EQUALITY,   // == !=
	PREC_COMPARISON, // < > <= >=
	PREC_OR,		 // or
	PREC_AND,		 // and
	PREC_TERM,		 // + -
	PREC_FACTOR,	 // * /
	PREC_UNARY,		 // ! -
	PREC_CALL,		 // . () []
	PREC_PRIMARY,
} Precedence;

typedef void (*ParseFn)(bluCompiler* compiler, bool canAssign);

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

static void errorAt(bluCompiler* compiler, bluToken* token, const char* message) {
	if (compiler->panicMode) return;
	compiler->panicMode = true;

	fprintf(stderr, "[line %d:%d] Error", token->line, token->column);

	if (token->type == TOKEN_EOF) {
		fprintf(stderr, " at end");
	} else if (token->type == TOKEN_NEWLINE) {
		fprintf(stderr, " at newline");
	} else if (token->type == TOKEN_ERROR) {
		// Nothing.
	} else {
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);

	compiler->hadError = true;
}

static void error(bluCompiler* compiler, const char* message) {
	errorAt(compiler, &compiler->previous, message);
}

static void errorAtCurrent(bluCompiler* compiler, const char* message) {
	errorAt(compiler, &compiler->current, message);
}

static void consumeNewlines(bluCompiler* compiler) {
	while (compiler->current.type == TOKEN_NEWLINE) {
		compiler->current = bluParserNextToken(compiler->parser);
	}
}

static void skipNewlines(bluCompiler* compiler) {
	switch (compiler->previous.type) {
	case TOKEN_NEWLINE:
	case TOKEN_LEFT_BRACE:
	case TOKEN_RIGHT_BRACE:
	case TOKEN_SEMICOLON:
	case TOKEN_DOT: consumeNewlines(compiler); break;

	default:
		// Do nothing.
		break;
	}
}

static void advance(bluCompiler* compiler) {
	compiler->previous = compiler->current;

	while (true) {
		compiler->current = bluParserNextToken(compiler->parser);
		if (compiler->current.type != TOKEN_ERROR) break;

		errorAtCurrent(compiler, compiler->current.start);
	}

	skipNewlines(compiler);
}

// Checks whether next token is of the given type.
// Returns true if so, otherwise returns false.
static bool check(bluCompiler* compiler, bluTokenType type) {
	if (type == TOKEN_NEWLINE && compiler->previous.type == TOKEN_RIGHT_BRACE) return true;

	return compiler->current.type == type;
}

// Consumes next token. If next token is not of the given type, throws an error with a message.
static void consume(bluCompiler* compiler, bluTokenType type, const char* message) {
	if (check(compiler, type)) {
		advance(compiler);
		return;
	}

	errorAtCurrent(compiler, message);
}

// Checks whether next token is of the given type.
// If yes, consumes it and returns true, otherwise it does not consume any tokens and return false.
static bool match(bluCompiler* compiler, bluTokenType type) {
	if (!check(compiler, type)) return false;

	advance(compiler);

	return true;
}

static void expectNewlineOrSemicolon(bluCompiler* compiler) {
	if (!match(compiler, TOKEN_SEMICOLON)) {
		consume(compiler, TOKEN_NEWLINE, "Expect newline or ';' after variable declaration.");
	}
}

static void synchronize(bluCompiler* compiler) {
	compiler->panicMode = false;

	while (compiler->current.type != TOKEN_EOF) {
		if (compiler->previous.type == TOKEN_SEMICOLON) return;

		switch (compiler->current.type) {
		case TOKEN_CLASS:
		case TOKEN_FN:
		case TOKEN_VAR:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_WHILE:
		case TOKEN_RETURN: return;
		default:; // Do nothing.
		}

		advance(compiler);
	}
}

static void emitByte(bluCompiler* compiler, uint8_t _byte) {
	bluChunkWrite(&compiler->function->chunk, _byte, compiler->current.line, compiler->current.column);
}

static void emitShort(bluCompiler* compiler, uint16_t _short) {
	emitByte(compiler, (_short >> 8) & 0xff);
	emitByte(compiler, _short & 0xff);
}

// static void emitBytes(bluCompiler* compiler, uint8_t byte1, uint8_t byte2) {
// 	emitByte(compiler, byte1);
// 	emitByte(compiler, byte2);
// }

static uint16_t makeConstant(bluCompiler* compiler, bluValue value) {
	int32_t constant = bluValueBufferWrite(&compiler->function->chunk.constants, value);
	if (constant > LOCALS_MAX) {
		error(compiler, "Too many constants in one chunk.");
		return 0;
	}

	return (uint16_t)constant;
}

static uint16_t emitConstant(bluCompiler* compiler, bluValue value) {
	uint16_t constant = makeConstant(compiler, value);

	emitByte(compiler, OP_CONSTANT);
	emitShort(compiler, constant);

	return constant;
}

static int32_t emitJump(bluCompiler* compiler, bluOpCode code) {
	emitByte(compiler, code);
	emitShort(compiler, 0);

	return compiler->function->chunk.code.count - 2;
}

static void patchJump(bluCompiler* compiler, int32_t jump) {
	bluChunk* currentChunk = &compiler->function->chunk;

	int32_t length = currentChunk->code.count - jump - 2;

	currentChunk->code.data[jump] = (length >> 8) & 0xff;
	currentChunk->code.data[jump + 1] = length & 0xff;
}

static void emitLoop(bluCompiler* compiler, int32_t loopStart) {
	emitByte(compiler, OP_LOOP);

	int32_t offset = compiler->function->chunk.code.count - loopStart + 2;

	if (offset > UINT16_MAX) error(compiler, "Loop body too large.");

	emitShort(compiler, offset);
}

static bool identifiersEqual(bluToken* a, bluToken* b) {
	if (a->length != b->length) return false;

	return memcmp(a->start, b->start, a->length) == 0;
}

static uint16_t identifierConstant(bluCompiler* compiler, bluToken* name) {
	return makeConstant(compiler, OBJ_VAL(bluCopyString(compiler->vm, name->start, name->length)));
}

static ParseRule* getRule(bluTokenType type);
static void parsePrecedence(bluCompiler* compiler, Precedence precedence);

static void statement(bluCompiler* compiler);
static void declaration(bluCompiler* compiler);
static void expression(bluCompiler* compiler);

static void binary(bluCompiler* compiler, bool canAssign) {
	bluTokenType operatorType = compiler->previous.type;

	ParseRule* rule = getRule(operatorType);
	parsePrecedence(compiler, (Precedence)(rule->precedence + 1));

	switch (operatorType) {
	case TOKEN_EQUAL_EQUAL: emitByte(compiler, OP_EQUAL); break;
	case TOKEN_BANG_EQUAL: emitByte(compiler, OP_NOT_EQUAL); break;
	case TOKEN_GREATER: emitByte(compiler, OP_GREATER); break;
	case TOKEN_GREATER_EQUAL: emitByte(compiler, OP_GREATER_EQUAL); break;
	case TOKEN_LESS: emitByte(compiler, OP_LESS); break;
	case TOKEN_LESS_EQUAL: emitByte(compiler, OP_LESS_EQUAL); break;
	case TOKEN_MINUS: emitByte(compiler, OP_SUBTRACT); break;
	case TOKEN_PERCENT: emitByte(compiler, OP_REMINDER); break;
	case TOKEN_PLUS: emitByte(compiler, OP_ADD); break;
	case TOKEN_SLASH: emitByte(compiler, OP_DIVIDE); break;
	case TOKEN_STAR: emitByte(compiler, OP_MULTIPLY); break;
	default: return;
	}
}

static void unary(bluCompiler* compiler, bool canAssing) {
	bluTokenType operatorType = compiler->previous.type;

	parsePrecedence(compiler, PREC_UNARY);

	switch (operatorType) {
	case TOKEN_BANG: emitByte(compiler, OP_NOT); break;
	case TOKEN_MINUS: emitByte(compiler, OP_NEGATE); break;
	default: __builtin_unreachable();
	}
}

static int32_t resolveLocal(bluCompiler* compiler, bluToken* name) {
	for (int32_t i = compiler->locals.count - 1; i >= 0; i--) {
		bluLocal* local = &compiler->locals.data[i];
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error(compiler, "Cannot read local variable in its own initializer.");
			}

			return i;
		}
	}

	return -1;
}

static void namedVariable(bluCompiler* compiler, bluToken name, bool canAssign) {
	uint8_t getOp, setOp;

	int32_t arg = resolveLocal(compiler, &name);

	if (arg != -1) {
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
		// TODO : Upvalues
		// } else if ((arg = resolveUpvalue(compiler, &name)) != -1) {
		// 	getOp = OP_GET_UPVALUE;
		// 	setOp = OP_SET_UPVALUE;
	} else {
		arg = identifierConstant(compiler, &name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(compiler, TOKEN_EQUAL)) {
		expression(compiler);
		emitByte(compiler, setOp);
		emitShort(compiler, (uint16_t)arg);
	} else {
		emitByte(compiler, getOp);
		emitShort(compiler, (uint16_t)arg);
	}
}

static void variable(bluCompiler* compiler, bool canAssign) {
	namedVariable(compiler, compiler->previous, canAssign);
}

static void number(bluCompiler* compiler, bool canAssign) {
	double value = strtod(compiler->previous.start, NULL);
	emitConstant(compiler, NUMBER_VAL(value));
}

static void string(bluCompiler* compiler, bool canAssign) {
	bluObjString* string = bluCopyString(compiler->vm, compiler->previous.start + 1, compiler->previous.length - 2);
	emitConstant(compiler, OBJ_VAL(string));
}

static void literal(bluCompiler* compiler, bool canAssign) {
	switch (compiler->previous.type) {
	case TOKEN_FALSE: emitByte(compiler, OP_FALSE); break;
	case TOKEN_NIL: emitByte(compiler, OP_NIL); break;
	case TOKEN_TRUE: emitByte(compiler, OP_TRUE); break;
	default: __builtin_unreachable();
	}
}

ParseRule rules[] = {
	{NULL, NULL, PREC_NONE}, // TOKEN_AT
	{NULL, NULL, PREC_NONE}, // TOKEN_COLON
	{NULL, NULL, PREC_NONE}, // TOKEN_COMMA
	{NULL, NULL, PREC_CALL}, // TOKEN_DOT
	{NULL, NULL, PREC_NONE}, // TOKEN_LEFT_BRACE
	{NULL, NULL, PREC_CALL}, // TOKEN_LEFT_BACKET
	{NULL, NULL, PREC_CALL}, // TOKEN_LEFT_PAREN
	{NULL, NULL, PREC_NONE}, // TOKEN_RIGHT_BRACE
	{NULL, NULL, PREC_NONE}, // TOKEN_RIGHT_BRACKET
	{NULL, NULL, PREC_NONE}, // TOKEN_RIGHT_PAREN
	{NULL, NULL, PREC_NONE}, // TOKEN_SEMICOLON

	{NULL, binary, PREC_EQUALITY},   // TOKEN_BANG_EQUAL
	{unary, NULL, PREC_NONE},		 // TOKEN_BANG
	{NULL, binary, PREC_EQUALITY},   // TOKEN_EQUAL_EQUAL
	{NULL, binary, PREC_NONE},		 // TOKEN_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER
	{NULL, binary, PREC_COMPARISON}, // TOKEN_LESS_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_LESS
	{unary, binary, PREC_TERM},		 // TOKEN_MINUS
	{NULL, binary, PREC_FACTOR},	 // TOKEN_PERCENT
	{NULL, binary, PREC_TERM},		 // TOKEN_PLUS
	{NULL, binary, PREC_FACTOR},	 // TOKEN_SLASH
	{NULL, binary, PREC_FACTOR},	 // TOKEN_STAR

	{variable, NULL, PREC_NONE}, // TOKEN_IDENTIFIER
	{number, NULL, PREC_NONE},   // TOKEN_NUMBER
	{string, NULL, PREC_NONE},   // TOKEN_STRING

	{NULL, NULL, PREC_AND},		// TOKEN_AND
	{NULL, NULL, PREC_NONE},	// TOKEN_ASSERT
	{NULL, NULL, PREC_NONE},	// TOKEN_BREAK
	{NULL, NULL, PREC_NONE},	// TOKEN_CLASS
	{NULL, NULL, PREC_NONE},	// TOKEN_ELSE
	{literal, NULL, PREC_NONE}, // TOKEN_FALSE
	{NULL, NULL, PREC_NONE},	// TOKEN_FN
	{NULL, NULL, PREC_NONE},	// TOKEN_FOR
	{NULL, NULL, PREC_NONE},	// TOKEN_IF
	{literal, NULL, PREC_NONE}, // TOKEN_NIL
	{NULL, NULL, PREC_OR},		// TOKEN_OR
	{NULL, NULL, PREC_NONE},	// TOKEN_RETURN
	{NULL, NULL, PREC_NONE},	// TOKEN_SUPER
	{literal, NULL, PREC_NONE}, // TOKEN_TRUE
	{NULL, NULL, PREC_NONE},	// TOKEN_VAR
	{NULL, NULL, PREC_NONE},	// TOKEN_WHILE

	{NULL, NULL, PREC_NONE}, // TOKEN_EOF
	{NULL, NULL, PREC_NONE}, // TOKEN_NEWLINE

	{NULL, NULL, PREC_NONE}, // TOKEN_ERROR
};

static ParseRule* getRule(bluTokenType type) {
	return &rules[type];
}

static void parsePrecedence(bluCompiler* compiler, Precedence precedence) {
	advance(compiler);

	ParseFn prefixRule = getRule(compiler->previous.type)->prefix;
	if (prefixRule == NULL) {
		error(compiler, "Expect expression.");
		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(compiler, canAssign);

	while (precedence <= getRule(compiler->current.type)->precedence) {
		advance(compiler);

		ParseFn infixRule = getRule(compiler->previous.type)->infix;
		infixRule(compiler, canAssign);
	}

	if (canAssign) {
		if (match(compiler, TOKEN_EQUAL)) {
			error(compiler, "Invalid assignment target.");

			// TODO
			// expression(compiler);
		}
	}
}

static void expression(bluCompiler* compiler) {
	parsePrecedence(compiler, PREC_ASSIGNMENT);
}

static void expressionStatement(bluCompiler* compiler) {
	expression(compiler);

	emitByte(compiler, OP_POP);

	if (!match(compiler, TOKEN_SEMICOLON)) {
		consume(compiler, TOKEN_NEWLINE, "Expect newline or ';' after expression statement.");
	}
}

static void beginScope(bluCompiler* compiler) {
	compiler->scopeDepth++;
}

static void endScope(bluCompiler* compiler) {
	compiler->scopeDepth--;

	while (compiler->locals.count > 0 &&
		   compiler->locals.data[compiler->locals.count - 1].depth > compiler->scopeDepth) {
		if (compiler->locals.data[compiler->locals.count - 1].isUpvalue) {
			// TODO : Upvalues
			// emitByte(compiler, OP_CLOSE_OPVALUE);
		} else {
			emitByte(compiler, OP_POP);
		}

		compiler->locals.count--;
	}
}

static void block(bluCompiler* compiler) {
	while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF)) {
		declaration(compiler);
	}

	consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void ifStatement(bluCompiler* compiler) {
	beginScope(compiler);

	expression(compiler);
	int32_t ifJump = emitJump(compiler, OP_JUMP_IF_FALSE);
	emitByte(compiler, OP_POP); // Condition

	// One-line if notation
	if (match(compiler, TOKEN_COLON)) {
		statement(compiler);

		int32_t elseJump = emitJump(compiler, OP_JUMP);
		patchJump(compiler, ifJump);
		emitByte(compiler, OP_POP); // Condition
		patchJump(compiler, elseJump);
		endScope(compiler);
		return;
	}

	consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' after if condition.");
	beginScope(compiler);
	block(compiler);
	endScope(compiler);

	int32_t elseJump = emitJump(compiler, OP_JUMP);
	patchJump(compiler, ifJump);
	emitByte(compiler, OP_POP); // Condition

	if (match(compiler, TOKEN_ELSE)) {
		if (match(compiler, TOKEN_LEFT_BRACE)) {
			beginScope(compiler);
			block(compiler);
			endScope(compiler);
		} else if (match(compiler, TOKEN_IF)) {
			ifStatement(compiler);
		} else {
			error(compiler, "Expect 'if' or '{' after 'else.'");
		}
	}

	patchJump(compiler, elseJump);

	endScope(compiler);
}

static void whileStatement(bluCompiler* compiler) {
	beginScope(compiler);

	int32_t loopStart = compiler->function->chunk.code.count;

	expression(compiler);

	int32_t exitJump = emitJump(compiler, OP_JUMP_IF_FALSE);
	emitByte(compiler, OP_POP); // Condition

	if (match(compiler, TOKEN_COLON)) {
		statement(compiler);
	} else {
		consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' after while condition.");

		beginScope(compiler);
		block(compiler);
		endScope(compiler);
	}

	emitLoop(compiler, loopStart);

	patchJump(compiler, exitJump);
	emitByte(compiler, OP_POP);

	endScope(compiler);
}

static void assertStatement(bluCompiler* compiler) {
	expression(compiler);
	emitByte(compiler, OP_ASSERT);

	expectNewlineOrSemicolon(compiler);
}

static void statement(bluCompiler* compiler) {
	if (match(compiler, TOKEN_LEFT_BRACE)) {
		beginScope(compiler);
		block(compiler);
		endScope(compiler);
	} else if (match(compiler, TOKEN_IF)) {
		ifStatement(compiler);
	} else if (match(compiler, TOKEN_WHILE)) {
		whileStatement(compiler);
	} else if (match(compiler, TOKEN_ASSERT)) {
		assertStatement(compiler);
	} else {
		expressionStatement(compiler);
	}
}

static void addLocal(bluCompiler* compiler, bluToken name) {
	if (compiler->locals.count == LOCALS_MAX) {
		error(compiler, "Too many local variables in function.");
		return;
	}

	bluLocal local;
	local.name = name;
	local.depth = -1;
	local.isUpvalue = false;

	bluLocalBufferWrite(&compiler->locals, local);
}

static void declareVariable(bluCompiler* compiler) {
	// Global variables are implicitly declared.
	if (compiler->scopeDepth == 0) return;

	bluToken* name = &compiler->previous;
	for (int32_t i = compiler->locals.count - 1; i >= 0; i--) {
		bluLocal* local = &compiler->locals.data[i];
		if (local->depth != -1 && local->depth < compiler->scopeDepth) break;
		if (identifiersEqual(name, &local->name)) {
			error(compiler, "Variable with this name already declared in this scope.");
		}
	}

	addLocal(compiler, *name);
}

static void markInitialized(bluCompiler* compiler) {
	if (compiler->scopeDepth == 0) return;

	compiler->locals.data[compiler->locals.count - 1].depth = compiler->scopeDepth;
}

static void defineVariable(bluCompiler* compiler, uint16_t index) {
	if (compiler->scopeDepth > 0) {
		markInitialized(compiler);
		return;
	}

	emitByte(compiler, OP_DEFINE_GLOBAL);
	emitShort(compiler, index);
}

static uint16_t parseVariable(bluCompiler* compiler, const char* errorMessage) {
	consume(compiler, TOKEN_IDENTIFIER, errorMessage);

	declareVariable(compiler);
	if (compiler->scopeDepth > 0) {
		return 0;
	}

	return identifierConstant(compiler, &compiler->previous);
}

static void varDeclaration(bluCompiler* compiler) {
	uint8_t name = parseVariable(compiler, "Expect variable name.");

	if (match(compiler, TOKEN_EQUAL)) {
		expression(compiler);
	} else {
		emitByte(compiler, OP_NIL);
	}

	expectNewlineOrSemicolon(compiler);

	defineVariable(compiler, name);
}

static void declaration(bluCompiler* compiler) {
	if (match(compiler, TOKEN_VAR)) {
		varDeclaration(compiler);
	} else {
		statement(compiler);
	}

	if (compiler->panicMode) synchronize(compiler);
}

void initCompiler(bluCompiler* compiler, bluCompiler* enclosing, int8_t scopeDepth, bluFunctionType type) {
	compiler->enclosing = enclosing;
	compiler->scopeDepth = scopeDepth;
	compiler->type = type;
	compiler->function = bluNewFunction(compiler->vm);

	bluLocalBufferInit(&compiler->locals);
	bluUpvalueBufferInit(&compiler->upvalues);

	switch (type) {
	case TYPE_TOP_LEVEL: compiler->function->name = NULL; break;
	}

	bluLocal local;
	local.depth = compiler->scopeDepth;
	local.isUpvalue = false;

	// TODO : Other function types
	// In a function, it holds the function, but cannot be references, so has no name.
	local.name.start = "";
	local.name.length = 0;

	bluLocalBufferWrite(&compiler->locals, local);
}

bluObjFunction* endCompiler(bluCompiler* compiler) {
	emitByte(compiler, OP_RETURN);

	bluLocalBufferFree(&compiler->locals);
	bluUpvalueBufferFree(&compiler->upvalues);

	return compiler->function;
}

bluObjFunction* bluCompilerCompile(bluVM* vm, bluCompiler* compiler, const char* source, const char* name) {
	compiler->vm = vm;
	compiler->parser = bluAllocate(vm, sizeof(bluParser));
	bluParserInit(compiler->parser, source);

	initCompiler(compiler, NULL, 0, TYPE_TOP_LEVEL);

	compiler->hadError = false;
	compiler->panicMode = false;

	do {
		advance(compiler);
	} while (check(compiler, TOKEN_NEWLINE));

	while (!match(compiler, TOKEN_EOF)) {
		declaration(compiler);
	}

	bluObjFunction* function = endCompiler(compiler);
	function->chunk.name = name;

#ifdef DEBUG_COMPILER_DISASSEMBLE
	bluDisassembleChunk(&function->chunk);
#endif

	bluDeallocate(vm, compiler->parser, sizeof(bluParser));

	return compiler->hadError ? NULL : function;
}
