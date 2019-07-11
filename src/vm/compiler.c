#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

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

static bluChunk* currentChunk(bluCompiler* compiler) {
	return &compiler->function->chunk;
}

static void errorAt(bluCompiler* compiler, bluToken* token, const char* message) {
	if (compiler->parser->panicMode) return;
	compiler->parser->panicMode = true;

	fprintf(stderr, "[line %d] Error", token->line);

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
	compiler->parser->hadError = true;
}

static void error(bluCompiler* compiler, const char* message) {
	errorAt(compiler, &compiler->parser->previous, message);
}

static void errorAtCurrent(bluCompiler* compiler, const char* message) {
	errorAt(compiler, &compiler->parser->current, message);
}

static void consumeNewlines(bluCompiler* compiler) {
	while (compiler->parser->current.type == TOKEN_NEWLINE) {
		compiler->parser->current = bluScanToken(compiler->scanner);
	}
}

static void skipNewlines(bluCompiler* compiler) {
	switch (compiler->parser->previous.type) {
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

// Scans another token, skipping error tokens and sets compiler->parser->previous to parse.current.
static void advance(bluCompiler* compiler) {
	compiler->parser->previous = compiler->parser->current;

	for (;;) {
		compiler->parser->current = bluScanToken(compiler->scanner);
		if (compiler->parser->current.type != TOKEN_ERROR) break;

		errorAtCurrent(compiler, compiler->parser->current.start);
	}

	skipNewlines(compiler);
}

// Checks whether next token is of the given type.
// Returns true if so, otherwise returns false.
static bool check(bluCompiler* compiler, bluTokenType type) {
	if (type == TOKEN_NEWLINE && compiler->parser->previous.type == TOKEN_RIGHT_BRACE) return true;

	return compiler->parser->current.type == type;
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

static void emitByte(bluCompiler* compiler, uint8_t byte) {
	bluWriteChunk(compiler->vm, currentChunk(compiler), byte, compiler->parser->previous.line);
}

static void emitBytes(bluCompiler* compiler, uint8_t byte1, uint8_t byte2) {
	emitByte(compiler, byte1);
	emitByte(compiler, byte2);
}

static void emitLoop(bluCompiler* compiler, int loopStart) {
	emitByte(compiler, OP_LOOP);

	int offset = currentChunk(compiler)->count - loopStart + 2;
	if (offset > UINT16_MAX) error(compiler, "Loop body too large.");

	emitByte(compiler, (offset >> 8) & 0xff);
	emitByte(compiler, offset & 0xff);
}

static void emitReturn(bluCompiler* compiler) {
	// An initializer automatically returns "@".
	if (compiler->type == TYPE_INITIALIZER) {
		emitBytes(compiler, OP_GET_LOCAL, 0);
	} else {
		emitByte(compiler, OP_NIL);
	}

	emitByte(compiler, OP_RETURN);
}

static uint8_t makeConstant(bluCompiler* compiler, bluValue value) {
	int constant = bluAddConstant(compiler->vm, currentChunk(compiler), value);
	if (constant > UINT8_MAX) {
		error(compiler, "Too many constants in one chunk.");
		return 0;
	}

	return (uint8_t)constant;
}

static uint8_t identifierConstant(bluCompiler* compiler, bluToken* name) {
	return makeConstant(compiler, OBJ_VAL(bluCopyString(compiler->vm, name->start, name->length)));
}

static uint8_t emitConstant(bluCompiler* compiler, bluValue value) {
	uint8_t constant = makeConstant(compiler, value);

	emitBytes(compiler, OP_CONSTANT, constant);

	return constant;
}

static int emitJump(bluCompiler* compiler, bluOpCode code) {
	emitByte(compiler, code);
	emitBytes(compiler, 0, 0);

	return currentChunk(compiler)->count - 2;
}

static void patchJump(bluCompiler* compiler, int jump) {
	int length = currentChunk(compiler)->count - jump - 2;

	currentChunk(compiler)->code[jump] = (length >> 8) & 0xff;
	currentChunk(compiler)->code[jump + 1] = length & 0xff;
}

void bluInitCompiler(bluVM* vm, bluCompiler* compiler, bluScanner* scanner, bluParser* parser, bluCompiler* current,
					 int scopeDepth, bluFunctionType type) {
	compiler->scanner = scanner;
	compiler->vm = vm;
	compiler->parser = parser;
	compiler->enclosing = current;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = scopeDepth;
	compiler->function = bluNewFunction(vm);

	compiler->isPrivate = false;
	compiler->inLoop = false;
	compiler->currentBreak = 0;

	if (current != NULL) {
		compiler->classCompiler = current->classCompiler;
	}

	switch (type) {
	case TYPE_INITIALIZER:
	case TYPE_METHOD:
	case TYPE_FUNCTION:
		compiler->function->name =
			bluCopyString(compiler->vm, compiler->parser->previous.start, compiler->parser->previous.length);
		break;
	case TYPE_ANONYMOUS:
	case TYPE_TOP_LEVEL: compiler->function->name = NULL; break;
	}

	// The first slot is always implicitly declared.
	bluLocal* local = &compiler->locals[compiler->localCount++];
	local->depth = compiler->scopeDepth;
	local->isUpvalue = false;

	if (type != TYPE_FUNCTION && type != TYPE_ANONYMOUS) {
		// In a method, it holds the receiver, "@".
		local->name.start = "@";
		local->name.length = 1;
	} else {
		// In a function, it holds the function, but cannot be referenced, so has no name.
		local->name.start = "";
		local->name.length = 0;
	}
}

void bluInitParser(bluParser* parser) {
	parser->hadError = false;
	parser->panicMode = false;
}

static bluObjFunction* endCompiler(bluCompiler* compiler) {
	if (compiler->currentBreak != 0) {
		patchJump(compiler, compiler->currentBreak);
	}

	emitReturn(compiler);

	bluObjFunction* function = compiler->function;

#ifdef DEBUG_PRINT_CODE
	if (!compiler->parser->hadError) {
		bluDisassembleChunk(compiler->vm, currentChunk(compiler),
							function->name != NULL ? function->name->chars : "<top>");
	}
#endif

	return function;
}

static void beginScope(bluCompiler* compiler) {
	compiler->scopeDepth++;
}

static void endScope(bluCompiler* compiler) {
	compiler->scopeDepth--;

	while (compiler->localCount > 0 && compiler->locals[compiler->localCount - 1].depth > compiler->scopeDepth) {
		if (compiler->locals[compiler->localCount - 1].isUpvalue) {
			emitByte(compiler, OP_CLOSE_UPVALUE);
		} else {
			emitByte(compiler, OP_POP);
		}
		compiler->localCount--;
	}
}

static void expression(bluCompiler* compiler);
static void statement(bluCompiler* compiler);
static void declaration(bluCompiler* compiler);
static ParseRule* getRule(bluCompiler* compiler, bluTokenType type);
static void parsePrecedence(bluCompiler* compiler, Precedence precedence);

static void binary(bluCompiler* compiler, bool canAssign) {
	// Remember the operator.
	bluTokenType operatorType = compiler->parser->previous.type;

	// Compile the right operand.
	ParseRule* rule = getRule(compiler, operatorType);
	parsePrecedence(compiler, (Precedence)(rule->precedence + 1));

	// Emit the operator instruction.
	switch (operatorType) {
	case TOKEN_BANG_EQUAL: emitBytes(compiler, OP_EQUAL, OP_NOT); break;
	case TOKEN_EQUAL_EQUAL: emitByte(compiler, OP_EQUAL); break;
	case TOKEN_GREATER: emitByte(compiler, OP_GREATER); break;
	case TOKEN_GREATER_EQUAL: emitBytes(compiler, OP_LESS, OP_NOT); break;
	case TOKEN_LESS: emitByte(compiler, OP_LESS); break;
	case TOKEN_LESS_EQUAL: emitBytes(compiler, OP_GREATER, OP_NOT); break;
	case TOKEN_PERCENT: emitByte(compiler, OP_REMINDER); break;
	case TOKEN_PLUS: emitByte(compiler, OP_ADD); break;
	case TOKEN_MINUS: emitByte(compiler, OP_SUBTRACT); break;
	case TOKEN_STAR: emitByte(compiler, OP_MULTIPLY); break;
	case TOKEN_SLASH: emitByte(compiler, OP_DIVIDE); break;
	default: return; // Unreachable.
	}
}

static void and_(bluCompiler* compiler, bool canAssign) {
	int endJump = emitJump(compiler, OP_JUMP_IF_FALSE);

	emitByte(compiler, OP_POP);
	parsePrecedence(compiler, PREC_AND);

	patchJump(compiler, endJump);
}

static void or_(bluCompiler* compiler, bool canAssign) {
	int elseJump = emitJump(compiler, OP_JUMP_IF_FALSE);
	int endJump = emitJump(compiler, OP_JUMP);

	patchJump(compiler, elseJump);
	emitByte(compiler, OP_POP);

	parsePrecedence(compiler, PREC_OR);
	patchJump(compiler, endJump);
}

static uint8_t argumentList(bluCompiler* compiler) {
	uint8_t argCount = 0;
	if (!check(compiler, TOKEN_RIGHT_PAREN)) {
		do {
			expression(compiler);
			argCount++;

			if (argCount > 8) {
				error(compiler, "Cannot have more than 8 arguments.");
			}
		} while (match(compiler, TOKEN_COMMA));
	}

	consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void call(bluCompiler* compiler, bool canAssign) {
	uint8_t argCount = argumentList(compiler);
	emitByte(compiler, OP_CALL_0 + argCount);
}

static void dot(bluCompiler* compiler, bool canAssign) {
	consume(compiler, TOKEN_IDENTIFIER, "Expect property name after '.'.");
	uint8_t name = identifierConstant(compiler, &compiler->parser->previous);

	if (!compiler->isPrivate && *compiler->parser->previous.start == '_') {
		error(compiler, "Cannot access a private property.");
	}

	if (canAssign && match(compiler, TOKEN_EQUAL)) {
		expression(compiler);
		emitBytes(compiler, OP_SET_PROPERTY, name);
	} else if (match(compiler, TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList(compiler);
		emitBytes(compiler, OP_INVOKE_0 + argCount, name);
	} else {
		emitBytes(compiler, OP_GET_PROPERTY, name);
	}
}

static void arrayAccess(bluCompiler* compiler, bool canAssign) {
	if (match(compiler, TOKEN_RIGHT_BRACKET) && canAssign && match(compiler, TOKEN_EQUAL)) {
		expression(compiler);
		emitByte(compiler, OP_ARRAY_PUSH);
		return;
	}

	expression(compiler);
	consume(compiler, TOKEN_RIGHT_BRACKET, "Expect ']' after array access.");

	if (canAssign && match(compiler, TOKEN_EQUAL)) {
		expression(compiler);
		emitByte(compiler, OP_ARRAY_SET);
	} else {
		emitByte(compiler, OP_ARRAY_GET);
	}
}

static void literal(bluCompiler* compiler, bool canAssign) {
	switch (compiler->parser->previous.type) {
	case TOKEN_FALSE: emitByte(compiler, OP_FALSE); break;
	case TOKEN_NIL: emitByte(compiler, OP_NIL); break;
	case TOKEN_TRUE: emitByte(compiler, OP_TRUE); break;
	default: return; // Unreachable.
	}
}

static void grouping(bluCompiler* compiler, bool canAssign) {
	expression(compiler);
	consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bluCompiler* compiler, bool canAssign) {
	double value = strtod(compiler->parser->previous.start, NULL);
	emitConstant(compiler, NUMBER_VAL(value));
}

static void string(bluCompiler* compiler, bool canAssign) {
	emitConstant(compiler, OBJ_VAL(bluCopyString(compiler->vm, compiler->parser->previous.start + 1,
												 compiler->parser->previous.length - 2)));
}

static bool identifiersEqual(bluToken* a, bluToken* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(bluCompiler* compiler, bluToken* name) {
	for (int i = compiler->localCount - 1; i >= 0; i--) {
		bluLocal* local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error(compiler, "Cannot read local variable in its own initializer.");
			}
			return i;
		}
	}

	return -1;
}

// Adds an upvalue to [compiler]'s function with the given properties. Does not add one if an upvalue for that variable
// is already in the list. Returns the index of the upvalue.
static int addUpvalue(bluCompiler* compiler, uint8_t index, bool isLocal) {
	// Look for an existing one.
	int upvalueCount = compiler->function->upvalueCount;
	for (int i = 0; i < upvalueCount; i++) {
		bluUpvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal) {
			return i;
		}
	}

	// If we got here, it's a new upvalue.
	if (upvalueCount == UINT8_COUNT) {
		error(compiler, "Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

// Attempts to look up [name] in the functions enclosing the one being compiled by [compiler]. If found, it adds an
// upvalue for it to this compiler's list of upvalues (unless it's already in there) and returns its index. If not
// found, returns -1.
//
// If the name is found outside of the immediately enclosing function, this will flatten the closure and add upvalues to
// all of the intermediate functions so that it gets walked down to this one.
static int resolveUpvalue(bluCompiler* compiler, bluToken* name) {
	// If we are at the top level, we didn't find it.
	if (compiler->enclosing == NULL) return -1;

	// See if it's a local variable in the immediately enclosing function.
	int local = resolveLocal(compiler->enclosing, name);
	if (local != -1) {
		// Mark the local as an upvalue so we know to close it when it goes out of scope.
		compiler->enclosing->locals[local].isUpvalue = true;
		return addUpvalue(compiler, (uint8_t)local, true);
	}

	// See if it's an upvalue in the immediately enclosing function. In other words, if it's a local variable in a
	// non-immediately enclosing function. This "flattens" closures automatically: it adds upvalues to all of the
	// intermediate functions to get from the function where a local is declared all the way into the possibly deeply
	// nested function that is closing over it.
	int upvalue = resolveUpvalue(compiler->enclosing, name);
	if (upvalue != -1) {
		return addUpvalue(compiler, (uint8_t)upvalue, false);
	}

	// If we got here, we walked all the way up the parent chain and
	// couldn't find it.
	return -1;
}

static void addLocal(bluCompiler* compiler, bluToken name) {
	if (compiler->localCount == UINT8_COUNT) {
		error(compiler, "Too many local variables in function.");
		return;
	}

	bluLocal* local = &compiler->locals[compiler->localCount++];
	local->name = name;
	local->depth = -1;
	local->isUpvalue = false;
}

static void declareVariable(bluCompiler* compiler) {
	// Global variables are implicitly declared.
	if (compiler->scopeDepth == 0) return;

	bluToken* name = &compiler->parser->previous;
	for (int i = compiler->localCount - 1; i >= 0; i--) {
		bluLocal* local = &compiler->locals[i];
		if (local->depth != -1 && local->depth < compiler->scopeDepth) break;
		if (identifiersEqual(name, &local->name)) {
			error(compiler, "Variable with this name already declared in this scope.");
		}
	}

	addLocal(compiler, *name);
}

static void namedVariable(bluCompiler* compiler, bluToken name, bool canAssign) {
	uint8_t getOp, setOp;
	int arg = resolveLocal(compiler, &name);
	if (arg != -1) {
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	} else if ((arg = resolveUpvalue(compiler, &name)) != -1) {
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	} else {
		arg = identifierConstant(compiler, &name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(compiler, TOKEN_EQUAL)) {
		expression(compiler);
		emitBytes(compiler, setOp, (uint8_t)arg);
	} else {
		emitBytes(compiler, getOp, (uint8_t)arg);
	}
}

static void variable(bluCompiler* compiler, bool canAssign) {
	namedVariable(compiler, compiler->parser->previous, canAssign);
}

static bluToken syntheticToken(const char* text) {
	bluToken token;
	token.start = text;
	token.length = (int)strlen(text);
	return token;
}

static void pushSuperclass(bluCompiler* compiler) {
	if (compiler->classCompiler == NULL) return;
	namedVariable(compiler, syntheticToken("super"), false);
}

static void super_(bluCompiler* compiler, bool canAssign) {
	if (compiler->classCompiler == NULL) {
		error(compiler, "Cannot use 'super' outside of a class.");
	} else if (!compiler->classCompiler->hasSuperclass) {
		error(compiler, "Cannot use 'super' in a class with no superclass.");
	}

	if (match(compiler, TOKEN_LEFT_PAREN)) {
		namedVariable(compiler, syntheticToken("@"), false);

		uint8_t argCount = argumentList(compiler);

		pushSuperclass(compiler);
		emitBytes(compiler, OP_SUPER_0 + argCount,
				  makeConstant(compiler, OBJ_VAL(bluCopyString(compiler->vm, "__init", 6))));
		return;
	}

	consume(compiler, TOKEN_DOT, "Expect '.' after 'super'.");
	consume(compiler, TOKEN_IDENTIFIER, "Expect superclass method name.");
	uint8_t name = identifierConstant(compiler, &compiler->parser->previous);

	// Push the receiver.
	namedVariable(compiler, syntheticToken("@"), false);

	if (match(compiler, TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList(compiler);

		pushSuperclass(compiler);
		emitBytes(compiler, OP_SUPER_0 + argCount, name);
	} else {
		pushSuperclass(compiler);
		emitBytes(compiler, OP_GET_SUPER, name);
	}
}

static void at(bluCompiler* compiler, bool canAssign) {
	if (compiler->classCompiler == NULL) {
		error(compiler, "Cannot use '@' outside of a class.");
	} else {
		variable(compiler, false);
		if (check(compiler, TOKEN_IDENTIFIER)) {
			compiler->isPrivate = true;
			dot(compiler, canAssign);
			compiler->isPrivate = false;
		}
	}
}

static void unary(bluCompiler* compiler, bool canAssign) {
	bluTokenType operatorType = compiler->parser->previous.type;

	// Compile the operand.
	parsePrecedence(compiler, PREC_UNARY);

	// Emit the operator instruction.
	switch (operatorType) {
	case TOKEN_BANG: emitByte(compiler, OP_NOT); break;
	case TOKEN_MINUS: emitByte(compiler, OP_NEGATE); break;
	default: return; // Unreachable.
	}
}

static void array(bluCompiler* compiler, bool canAssign) {
	uint8_t len = 0;

	consumeNewlines(compiler);

	while (!match(compiler, TOKEN_RIGHT_BRACKET)) {
		if (len > 0) {
			consume(compiler, TOKEN_COMMA, "Expect ',' between expressions.");
			consumeNewlines(compiler);
		}

		expression(compiler);
		consumeNewlines(compiler);

		len++;
	}

	emitBytes(compiler, OP_ARRAY, len);
}

ParseRule rules[] = {
	{grouping, call, PREC_CALL},	 // TOKEN_LEFT_PAREN
	{NULL, NULL, PREC_NONE},		 // TOKEN_RIGHT_PAREN
	{array, arrayAccess, PREC_CALL}, // TOKEN_LEFT_BACKET
	{NULL, NULL, PREC_NONE},		 // TOKEN_RIGHT_BRACKET
	{NULL, NULL, PREC_NONE},		 // TOKEN_LEFT_BRACE
	{NULL, NULL, PREC_NONE},		 // TOKEN_RIGHT_BRACE
	{NULL, NULL, PREC_NONE},		 // TOKEN_COLON
	{NULL, NULL, PREC_NONE},		 // TOKEN_COMMA
	{NULL, dot, PREC_CALL},			 // TOKEN_DOT
	{NULL, NULL, PREC_NONE},		 // TOKEN_SEMICOLON
	{at, NULL, PREC_NONE},			 // TOKEN_AT

	{unary, NULL, PREC_NONE},		 // TOKEN_BANG
	{NULL, binary, PREC_EQUALITY},   // TOKEN_BANG_EQUAL
	{NULL, NULL, PREC_NONE},		 // TOKEN_EQUAL
	{NULL, binary, PREC_EQUALITY},   // TOKEN_EQUAL_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER
	{NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_LESS
	{NULL, binary, PREC_COMPARISON}, // TOKEN_LESS_EQUAL
	{unary, binary, PREC_TERM},		 // TOKEN_MINUS
	{NULL, binary, PREC_FACTOR},	 // TOKEN_PERCENT
	{NULL, binary, PREC_TERM},		 // TOKEN_PLUS
	{NULL, binary, PREC_FACTOR},	 // TOKEN_SLASH
	{NULL, binary, PREC_FACTOR},	 // TOKEN_STAR

	{variable, NULL, PREC_NONE}, // TOKEN_IDENTIFIER
	{string, NULL, PREC_NONE},   // TOKEN_STRING
	{number, NULL, PREC_NONE},   // TOKEN_NUMBER

	{NULL, and_, PREC_AND},		// TOKEN_AND
	{NULL, NULL, PREC_NONE},	// TOKEN_BREAK
	{NULL, NULL, PREC_NONE},	// TOKEN_CLASS
	{NULL, NULL, PREC_NONE},	// TOKEN_ELSE
	{literal, NULL, PREC_NONE}, // TOKEN_FALSE
	{NULL, NULL, PREC_NONE},	// TOKEN_FOR
	{NULL, NULL, PREC_NONE},	// TOKEN_FN
	{NULL, NULL, PREC_NONE},	// TOKEN_IF
	{literal, NULL, PREC_NONE}, // TOKEN_NIL
	{NULL, or_, PREC_OR},		// TOKEN_OR
	{NULL, NULL, PREC_NONE},	// TOKEN_ASSERT
	{NULL, NULL, PREC_NONE},	// TOKEN_RETURN
	{super_, NULL, PREC_NONE},  // TOKEN_SUPER
	{literal, NULL, PREC_NONE}, // TOKEN_TRUE
	{NULL, NULL, PREC_NONE},	// TOKEN_VAR
	{NULL, NULL, PREC_NONE},	// TOKEN_WHILE

	{NULL, NULL, PREC_NONE}, // TOKEN_ERROR
	{NULL, NULL, PREC_NONE}, // TOKEN_NEWLINE
	{NULL, NULL, PREC_NONE}, // TOKEN_EOF
};

static void parsePrecedence(bluCompiler* compiler, Precedence precedence) {
	advance(compiler);
	ParseFn prefixRule = getRule(compiler, compiler->parser->previous.type)->prefix;
	if (prefixRule == NULL) {
		error(compiler, "Expect expression.");
		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(compiler, canAssign);

	while (precedence <= getRule(compiler, compiler->parser->current.type)->precedence) {
		advance(compiler);
		ParseFn infixRule = getRule(compiler, compiler->parser->previous.type)->infix;
		infixRule(compiler, canAssign);
	}

	if (canAssign) {
		if (match(compiler, TOKEN_EQUAL)) {
			error(compiler, "Invalid assignment target.");
			expression(compiler);
		}
	}
}

static uint8_t parseVariable(bluCompiler* compiler, const char* errorMessage) {
	consume(compiler, TOKEN_IDENTIFIER, errorMessage);

	declareVariable(compiler);
	if (compiler->scopeDepth > 0) return 0;

	return identifierConstant(compiler, &compiler->parser->previous);
}

static void markInitialized(bluCompiler* compiler) {
	if (compiler->scopeDepth == 0) return;
	compiler->locals[compiler->localCount - 1].depth = compiler->scopeDepth;
}

static void defineVariable(bluCompiler* compiler, uint8_t global) {
	if (compiler->scopeDepth > 0) {
		markInitialized(compiler);
		return;
	}

	emitBytes(compiler, OP_DEFINE_GLOBAL, global);
}

static ParseRule* getRule(bluCompiler* compiler, bluTokenType type) {
	return &rules[type];
}

static void block(bluCompiler* compiler) {
	while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF)) {
		declaration(compiler);
	}

	consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(bluCompiler* enclosing, bluFunctionType type) {
	bluCompiler compiler;
	bluInitCompiler(enclosing->vm, &compiler, enclosing->scanner, enclosing->parser, enclosing, 1, type);

	// Compile the parameter list.
	consume(&compiler, TOKEN_LEFT_PAREN, "Expect '(' after function name.");

	if (!check(&compiler, TOKEN_RIGHT_PAREN)) {
		do {
			uint8_t paramConstant = parseVariable(&compiler, "Expect parameter name.");
			defineVariable(&compiler, paramConstant);

			compiler.function->arity++;
			if (compiler.function->arity > 8) {
				error(&compiler, "Cannot have more than 8 parameters.");
			}
		} while (match(&compiler, TOKEN_COMMA));
	}

	consume(&compiler, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

	// The body.
	if (type == TYPE_ANONYMOUS && match(&compiler, TOKEN_COLON)) {
		expression(&compiler);
		emitByte(&compiler, OP_RETURN);
	} else if (type != TYPE_INITIALIZER && match(&compiler, TOKEN_COLON)) {
		expression(&compiler);
		if (!match(&compiler, TOKEN_SEMICOLON))
			consume(&compiler, TOKEN_NEWLINE, "Expect newline or ';' after function declaration.");
		emitByte(&compiler, OP_RETURN);
	} else {
		consume(&compiler, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
		beginScope(&compiler);
		block(&compiler);
		endScope(&compiler);
	}

	// Create the function object.
	bluObjFunction* function = endCompiler(&compiler);

	// Capture the upvalues in the new closure object.
	emitBytes(&compiler, OP_CLOSURE, makeConstant(&compiler, OBJ_VAL(function)));

	// Emit arguments for each upvalue to know whether to capture a local or an upvalue.
	for (int i = 0; i < function->upvalueCount; i++) {
		emitByte(&compiler, compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(&compiler, compiler.upvalues[i].index);
	}
}

static void expression(bluCompiler* compiler) {
	if (match(compiler, TOKEN_FN)) {
		function(compiler, TYPE_ANONYMOUS);
	} else {
		parsePrecedence(compiler, PREC_ASSIGNMENT);
	}
}

static void method(bluCompiler* compiler) {
	consume(compiler, TOKEN_IDENTIFIER, "Expect method name.");
	uint8_t constant = identifierConstant(compiler, &compiler->parser->previous);

	// If the method is named "init", it's an initializer.
	bluFunctionType type = TYPE_METHOD;
	if (compiler->parser->previous.length == 6 && memcmp(compiler->parser->previous.start, "__init", 6) == 0) {
		type = TYPE_INITIALIZER;
	}

	function(compiler, type);

	emitBytes(compiler, OP_METHOD, constant);
}

static void classDeclaration(bluCompiler* compiler) {
	uint8_t name = parseVariable(compiler, "Expect class name.");
	bluToken className = compiler->parser->previous;

	emitBytes(compiler, OP_CLASS, name);
	defineVariable(compiler, name);

	bluClassCompiler classCompiler;
	classCompiler.name = className;
	classCompiler.hasSuperclass = false;
	classCompiler.enclosing = compiler->classCompiler;

	compiler->classCompiler = &classCompiler;

	if (match(compiler, TOKEN_LESS)) {
		consume(compiler, TOKEN_IDENTIFIER, "Expect superclass name.");

		if (identifiersEqual(&className, &compiler->parser->previous)) {
			error(compiler, "A class cannot inherit from itself.");
		}

		classCompiler.hasSuperclass = true;

		beginScope(compiler);

		// Store the superclass in a local variable named "super".
		variable(compiler, false);
		addLocal(compiler, syntheticToken("super"));
		defineVariable(compiler, 0);

		namedVariable(compiler, className, false);
		emitByte(compiler, OP_INHERIT);
	} else if (strncmp("Object", compiler->parser->previous.start, 6) != 0) {
		classCompiler.hasSuperclass = true;

		beginScope(compiler);

		namedVariable(compiler, syntheticToken("Object"), false);
		addLocal(compiler, syntheticToken("super"));
		defineVariable(compiler, 0);

		namedVariable(compiler, className, false);
		emitByte(compiler, OP_INHERIT);
	}

	consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before class body.");

	while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF)) {
		if (match(compiler, TOKEN_FN)) {
			namedVariable(compiler, className, false);
			method(compiler);
		} else {
			errorAtCurrent(compiler, "Expect method declaration.");
			return;
		}
	}

	consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

	if (classCompiler.hasSuperclass) {
		endScope(compiler);
	}

	compiler->classCompiler = compiler->classCompiler->enclosing;
}

static void fnDeclaration(bluCompiler* compiler) {
	uint8_t name = parseVariable(compiler, "Expect function name.");
	markInitialized(compiler);

	function(compiler, TYPE_FUNCTION);

	defineVariable(compiler, name);
}

static void varDeclaration(bluCompiler* compiler) {
	uint8_t name = parseVariable(compiler, "Expect variable name.");

	if (match(compiler, TOKEN_EQUAL)) {
		expression(compiler);
	} else {
		emitByte(compiler, OP_NIL);
	}

	if (!match(compiler, TOKEN_SEMICOLON))
		consume(compiler, TOKEN_NEWLINE, "Expect newline or ';' after variable declaration.");

	defineVariable(compiler, name);
}

static void expressionStatement(bluCompiler* compiler) {
	expression(compiler);
	emitByte(compiler, OP_POP);
	if (!match(compiler, TOKEN_SEMICOLON))
		consume(compiler, TOKEN_NEWLINE, "Expect newline or ';' after expression statement.");
}

static void assertStatement(bluCompiler* compiler) {
	expression(compiler);
	if (!match(compiler, TOKEN_SEMICOLON)) consume(compiler, TOKEN_NEWLINE, "Expect newline or ';' after value.");
	emitByte(compiler, OP_ASSERT);
}

static void ifStatement(bluCompiler* compiler) {
	beginScope(compiler);

	expression(compiler);
	int ifJump = emitJump(compiler, OP_JUMP_IF_FALSE);
	emitByte(compiler, OP_POP); // Condition

	// One-line if notation
	if (match(compiler, TOKEN_COLON)) {
		statement(compiler);

		int elseJump = emitJump(compiler, OP_JUMP);
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

	int elseJump = emitJump(compiler, OP_JUMP);
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
			error(compiler, "Expect 'if' or '{' after 'else'.");
		}
	}

	patchJump(compiler, elseJump);

	endScope(compiler);
}

static void returnStatement(bluCompiler* compiler) {
	if (compiler->type == TYPE_TOP_LEVEL) {
		error(compiler, "Cannot return from top-level code.");
	}

	if (match(compiler, TOKEN_NEWLINE)) {
		emitReturn(compiler);
	} else {
		bool needsNewline = !check(compiler, TOKEN_FN);

		if (compiler->type == TYPE_INITIALIZER) {
			error(compiler, "Cannot return a value from an initializer.");
		}

		expression(compiler);
		emitByte(compiler, OP_RETURN);

		if (needsNewline && !match(compiler, TOKEN_SEMICOLON))
			consume(compiler, TOKEN_NEWLINE, "Expect newline or ';' after return value.");
	}
}

static void whileStatement(bluCompiler* compiler) {
	bool inLoop = compiler->inLoop;
	compiler->inLoop = true;

	int currentBreak = compiler->currentBreak;

	int loopStart = currentChunk(compiler)->count;

	expression(compiler);

	int exitJump = emitJump(compiler, OP_JUMP_IF_FALSE);
	emitByte(compiler, OP_POP);

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

	if (compiler->currentBreak != 0) {
		patchJump(compiler, compiler->currentBreak);
	}

	compiler->inLoop = inLoop;
	compiler->currentBreak = currentBreak;
}

static void forStatement(bluCompiler* compiler) {
	bool inLoop = compiler->inLoop;
	compiler->inLoop = true;

	int currentBreak = compiler->currentBreak;

	beginScope(compiler);

	// The initialization clause.
	if (match(compiler, TOKEN_VAR)) {
		varDeclaration(compiler);
	} else if (match(compiler, TOKEN_SEMICOLON)) {
		// No initializer.
	} else {
		expressionStatement(compiler);
	}

	int loopStart = currentChunk(compiler)->count;

	// The exit condition.
	int exitJump = 0;
	if (!match(compiler, TOKEN_SEMICOLON)) {
		expression(compiler);
		consume(compiler, TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emitJump(compiler, OP_JUMP_IF_FALSE);
		emitByte(compiler, OP_POP); // Condition
	}

	// Increment step.
	if (!match(compiler, TOKEN_LEFT_BRACE)) {
		// We don't want to execute the increment before the body, so jump over it.
		int bodyJump = emitJump(compiler, OP_JUMP);

		int incrementStart = currentChunk(compiler)->count;
		expression(compiler);
		emitByte(compiler, OP_POP);

		// After the increment, start the whole loop over.
		emitLoop(compiler, loopStart);

		// At the end of the body, we want to jump to the increment, not the top of the loop.
		loopStart = incrementStart;

		patchJump(compiler, bodyJump);
	}

	// Compile the body.
	if (match(compiler, TOKEN_COLON)) {
		statement(compiler);
	} else {
		consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' after for clause.");
		beginScope(compiler);
		block(compiler);
		endScope(compiler);
	}

	// Jump back to the beginning (or the increment).
	emitLoop(compiler, loopStart);

	if (exitJump != 0) {
		patchJump(compiler, exitJump);
		emitByte(compiler, OP_POP); // Condition
	}

	if (compiler->currentBreak != 0) {
		patchJump(compiler, compiler->currentBreak);
	}

	endScope(compiler);

	compiler->inLoop = inLoop;
	compiler->currentBreak = currentBreak;
}

static void breakStatement(bluCompiler* compiler) {
	if (compiler->inLoop == false) {
		error(compiler, "Break statement cannot be used outside of loop.");
	}

	if (!match(compiler, TOKEN_SEMICOLON))
		consume(compiler, TOKEN_NEWLINE, "Expect newline or ';' after break statement.");

	compiler->currentBreak = emitJump(compiler, OP_JUMP);
}

static void synchronize(bluCompiler* compiler) {
	compiler->parser->panicMode = false;

	while (compiler->parser->current.type != TOKEN_EOF) {
		if (compiler->parser->previous.type == TOKEN_SEMICOLON) return;

		switch (compiler->parser->current.type) {
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

static void declaration(bluCompiler* compiler) {
	if (match(compiler, TOKEN_VAR)) {
		varDeclaration(compiler);
	} else if (match(compiler, TOKEN_FN)) {
		fnDeclaration(compiler);
	} else if (match(compiler, TOKEN_CLASS)) {
		classDeclaration(compiler);
	} else {
		statement(compiler);
	}

	if (compiler->parser->panicMode) synchronize(compiler);
}

static void statement(bluCompiler* compiler) {
	if (match(compiler, TOKEN_IF)) {
		ifStatement(compiler);
	} else if (match(compiler, TOKEN_WHILE)) {
		whileStatement(compiler);
	} else if (match(compiler, TOKEN_FOR)) {
		forStatement(compiler);
	} else if (match(compiler, TOKEN_BREAK)) {
		breakStatement(compiler);
	} else if (match(compiler, TOKEN_RETURN)) {
		returnStatement(compiler);
	} else if (match(compiler, TOKEN_LEFT_BRACE)) {
		beginScope(compiler);
		block(compiler);
		endScope(compiler);
	} else if (match(compiler, TOKEN_ASSERT)) {
		assertStatement(compiler);
	} else {
		expressionStatement(compiler);
	}
}

bluObjFunction* bluCompile(bluVM* vm, const char* source) {
	bluInitScanner(&vm->scanner, source);
	bluInitParser(&vm->parser);
	bluInitCompiler(vm, &vm->compiler, &vm->scanner, &vm->parser, NULL, 0, TYPE_TOP_LEVEL);

	do {
		advance(&vm->compiler);
	} while (check(&vm->compiler, TOKEN_NEWLINE));

	while (!match(&vm->compiler, TOKEN_EOF)) {
		declaration(&vm->compiler);
	}

	bluObjFunction* function = endCompiler(&vm->compiler);

	return vm->compiler.parser->hadError ? NULL : function;
}

void bluGrayCompilerRoots(bluVM* vm) {
	bluCompiler* compiler = &vm->compiler;

	while (compiler != NULL) {
		bluGrayObject(vm, (bluObj*)compiler->function);
		compiler = compiler->enclosing;
	}
}
