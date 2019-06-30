#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
	Token current;
	Token previous;
	bool hadError;
	bool panicMode;
} Parser;

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

typedef void (*ParseFn)(bool canAssign);

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

typedef struct {
	Token name;
	int depth;

	// True if this local variable is captured as an upvalue by a function.
	bool isUpvalue;
} Local;

typedef struct {
	// The index of the local variable or upvalue being captured from the enclosing function.
	uint8_t index;

	// Whether the captured variable is a local or upvalue in the enclosing function.
	bool isLocal;
} Upvalue;

typedef enum {
	TYPE_FUNCTION,
	TYPE_ANONYMOUS,
	TYPE_TOP_LEVEL,
} FunctionType;

typedef struct Compiler {
	// The compiler for the enclosing function, if any.
	struct Compiler* enclosing;

	// The function being compiled.
	ObjFunction* function;
	FunctionType type;

	Local locals[UINT8_COUNT];
	int localCount;
	Upvalue upvalues[UINT8_COUNT];
	int scopeDepth;

	bool inLoop;
	int currentBreak;
} Compiler;

Compiler* current = NULL;

Parser parser;

static Chunk* currentChunk() {
	return &current->function->chunk;
}

static void errorAt(Token* token, const char* message) {
	if (parser.panicMode) return;
	parser.panicMode = true;

	fprintf(stderr, "[line %d] Error", token->line);

	if (token->type == TOKEN_EOF) {
		fprintf(stderr, " at end");
	} else if (token->type == TOKEN_ERROR) {
		// Nothing.
	} else {
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	parser.hadError = true;
}

static void error(const char* message) {
	errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
	errorAt(&parser.current, message);
}

// Scans another token, skipping error tokens and sets parser.previous to parse.current.
static void advance() {
	parser.previous = parser.current;

	for (;;) {
		parser.current = scanToken();
		if (parser.current.type != TOKEN_ERROR) break;

		errorAtCurrent(parser.current.start);
	}
}

// Consumes next token. If next token is not of the given type, throws an error with a message.
static void consume(TokenType type, const char* message) {
	if (parser.current.type == type) {
		advance();
		return;
	}

	errorAtCurrent(message);
}

// Checks whether next token is of the given type.
// Returns true if so, otherwise returns false.
static bool check(TokenType type) {
	return parser.current.type == type;
}

// Checks whether next token is of the given type.
// If yes, consumes it and returns true, otherwise it does not consume any tokens and return false.
static bool match(TokenType type) {
	if (!check(type)) return false;
	advance();
	return true;
}

static void emitByte(uint8_t byte) {
	writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
	emitByte(byte1);
	emitByte(byte2);
}

static void emitLoop(int loopStart) {
	emitByte(OP_LOOP);

	int offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX) error("Loop body too large.");

	emitByte((offset >> 8) & 0xff);
	emitByte(offset & 0xff);
}

static void emitReturn() {
	emitByte(OP_NIL);
	emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
	int constant = addConstant(currentChunk(), value);
	if (constant > UINT8_MAX) {
		error("Too many constants in one chunk.");
		return 0;
	}

	return (uint8_t)constant;
}

static uint8_t emitConstant(Value value) {
	uint8_t constant = makeConstant(value);

	emitBytes(OP_CONSTANT, constant);

	return constant;
}

static int emitJump(OpCode code) {
	emitByte(code);
	emitBytes(0, 0);

	return currentChunk()->count - 2;
}

static void patchJump(int jump) {
	int length = currentChunk()->count - jump - 2;

	currentChunk()->code[jump] = (length >> 8) & 0xff;
	currentChunk()->code[jump + 1] = length & 0xff;
}

static void initCompiler(Compiler* compiler, int scopeDepth, FunctionType type) {
	compiler->enclosing = current;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = scopeDepth;
	compiler->function = newFunction();

	compiler->inLoop = false;
	compiler->currentBreak = 0;

	current = compiler;

	switch (type) {
	case TYPE_FUNCTION: current->function->name = copyString(parser.previous.start, parser.previous.length); break;
	case TYPE_ANONYMOUS: current->function->name = NULL; break;
	case TYPE_TOP_LEVEL: current->function->name = NULL; break;
	}

	// The first slot is always implicitly declared.
	Local* local = &current->locals[current->localCount++];
	local->depth = current->scopeDepth;
	local->isUpvalue = false;
	local->name.start = "";
	local->name.length = 0;
}

static ObjFunction* endCompiler() {
	if (current->currentBreak != 0) {
		patchJump(current->currentBreak);
	}

	emitReturn();

	ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError) {
		disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<top>");
	}
#endif

	current = current->enclosing;

	return function;
}

static void beginScope() {
	current->scopeDepth++;
}

static void endScope() {
	current->scopeDepth--;

	while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
		if (current->locals[current->localCount - 1].isUpvalue) {
			emitByte(OP_CLOSE_UPVALUE);
		} else {
			emitByte(OP_POP);
		}
		current->localCount--;
	}
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary(bool canAssign) {
	// Remember the operator.
	TokenType operatorType = parser.previous.type;

	// Compile the right operand.
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	// Emit the operator instruction.
	switch (operatorType) {
	case TOKEN_BANG_EQUAL: emitBytes(OP_EQUAL, OP_NOT); break;
	case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
	case TOKEN_GREATER: emitByte(OP_GREATER); break;
	case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
	case TOKEN_LESS: emitByte(OP_LESS); break;
	case TOKEN_LESS_EQUAL: emitBytes(OP_GREATER, OP_NOT); break;
	case TOKEN_PERCENT: emitByte(OP_REMINDER); break;
	case TOKEN_PLUS: emitByte(OP_ADD); break;
	case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
	case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
	case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
	default: return; // Unreachable.
	}
}

static void and_(bool canAssign) {
	int endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

static void or_(bool canAssign) {
	int elseJump = emitJump(OP_JUMP_IF_FALSE);
	int endJump = emitJump(OP_JUMP);

	patchJump(elseJump);
	emitByte(OP_POP);

	parsePrecedence(PREC_OR);
	patchJump(endJump);
}

static uint8_t argumentList() {
	uint8_t argCount = 0;
	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			expression();
			argCount++;

			if (argCount > 8) {
				error("Cannot have more than 8 arguments.");
			}
		} while (match(TOKEN_COMMA));
	}

	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void call(bool canAssign) {
	uint8_t argCount = argumentList();
	emitByte(OP_CALL_0 + argCount);
}

static void arrayAccess(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array access.");

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitByte(OP_ARRAY_SET);
	} else {
		emitByte(OP_ARRAY_GET);
	}
}

static void literal(bool canAssign) {
	switch (parser.previous.type) {
	case TOKEN_FALSE: emitByte(OP_FALSE); break;
	case TOKEN_NIL: emitByte(OP_NIL); break;
	case TOKEN_TRUE: emitByte(OP_TRUE); break;
	default: return; // Unreachable.
	}
}

static void grouping(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
	double value = strtod(parser.previous.start, NULL);
	emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static uint8_t identifierConstant(Token* name) {
	return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
	for (int i = compiler->localCount - 1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error("Cannot read local variable in its own initializer.");
			}
			return i;
		}
	}

	return -1;
}

// Adds an upvalue to [compiler]'s function with the given properties. Does not add one if an upvalue for that variable
// is already in the list. Returns the index of the upvalue.
static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
	// Look for an existing one.
	int upvalueCount = compiler->function->upvalueCount;
	for (int i = 0; i < upvalueCount; i++) {
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal) {
			return i;
		}
	}

	// If we got here, it's a new upvalue.
	if (upvalueCount == UINT8_COUNT) {
		error("Too many closure variables in function.");
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
static int resolveUpvalue(Compiler* compiler, Token* name) {
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

static void addLocal(Token name) {
	if (current->localCount == UINT8_COUNT) {
		error("Too many local variables in function.");
		return;
	}

	Local* local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1;
	local->isUpvalue = false;
}

static void declareVariable() {
	// Global variables are implicitly declared.
	if (current->scopeDepth == 0) return;

	Token* name = &parser.previous;
	for (int i = current->localCount - 1; i >= 0; i--) {
		Local* local = &current->locals[i];
		if (local->depth != -1 && local->depth < current->scopeDepth) break;
		if (identifiersEqual(name, &local->name)) {
			error("Variable with this name already declared in this scope.");
		}
	}

	addLocal(*name);
}

static void namedVariable(Token name, bool canAssign) {
	uint8_t getOp, setOp;
	int arg = resolveLocal(current, &name);
	if (arg != -1) {
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	} else if ((arg = resolveUpvalue(current, &name)) != -1) {
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	} else {
		arg = identifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitBytes(setOp, (uint8_t)arg);
	} else if (canAssign && match(TOKEN_MINUS_EQUAL)) {
		emitBytes(getOp, (uint8_t)arg);
		expression();
		emitByte(OP_SUBTRACT);
		emitBytes(setOp, (uint8_t)arg);
	} else if (canAssign && match(TOKEN_PLUS_EQUAL)) {
		emitBytes(getOp, (uint8_t)arg);
		expression();
		emitByte(OP_ADD);
		emitBytes(setOp, (uint8_t)arg);
	} else if (canAssign && match(TOKEN_SLASH_EQUAL)) {
		emitBytes(getOp, (uint8_t)arg);
		expression();
		emitByte(OP_DIVIDE);
		emitBytes(setOp, (uint8_t)arg);
	} else if (canAssign && match(TOKEN_STAR_EQUAL)) {
		emitBytes(getOp, (uint8_t)arg);
		expression();
		emitByte(OP_MULTIPLY);
		emitBytes(setOp, (uint8_t)arg);
	} else if (canAssign && match(TOKEN_PERCENT_EQUAL)) {
		emitBytes(getOp, (uint8_t)arg);
		expression();
		emitByte(OP_REMINDER);
		emitBytes(setOp, (uint8_t)arg);
	} else {
		emitBytes(getOp, (uint8_t)arg);
	}
}

static void variable(bool canAssign) {
	namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
	TokenType operatorType = parser.previous.type;

	// Compile the operand.
	parsePrecedence(PREC_UNARY);

	// Emit the operator instruction.
	switch (operatorType) {
	case TOKEN_BANG: emitByte(OP_NOT); break;
	case TOKEN_MINUS: emitByte(OP_NEGATE); break;
	default: return; // Unreachable.
	}
}

ParseRule rules[] = {
	{grouping, call, PREC_CALL},	// TOKEN_LEFT_PAREN
	{NULL, NULL, PREC_NONE},		// TOKEN_RIGHT_PAREN
	{NULL, arrayAccess, PREC_CALL}, // TOKEN_LEFT_BACKET
	{NULL, NULL, PREC_NONE},		// TOKEN_RIGHT_BRACKET
	{NULL, NULL, PREC_NONE},		// TOKEN_LEFT_BRACE
	{NULL, NULL, PREC_NONE},		// TOKEN_RIGHT_BRACE
	{NULL, NULL, PREC_NONE},		// TOKEN_COLON
	{NULL, NULL, PREC_NONE},		// TOKEN_COMMA
	{NULL, NULL, PREC_CALL},		// TOKEN_DOT
	{NULL, NULL, PREC_NONE},		// TOKEN_SEMICOLON

	{unary, NULL, PREC_NONE},		 // TOKEN_BANG
	{NULL, binary, PREC_EQUALITY},   // TOKEN_BANG_EQUAL
	{NULL, NULL, PREC_NONE},		 // TOKEN_EQUAL
	{NULL, binary, PREC_EQUALITY},   // TOKEN_EQUAL_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER
	{NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_LESS
	{NULL, binary, PREC_COMPARISON}, // TOKEN_LESS_EQUAL
	{unary, binary, PREC_TERM},		 // TOKEN_MINUS
	{NULL, NULL, PREC_NONE},		 // TOKEN_MINUS_EQUAL
	{NULL, binary, PREC_FACTOR},	 // TOKEN_PERCENT
	{NULL, NULL, PREC_NONE},		 // TOKEN_PERCENT_EQUAL
	{NULL, binary, PREC_TERM},		 // TOKEN_PLUS
	{NULL, NULL, PREC_NONE},		 // TOKEN_PLUS_EQUAL
	{NULL, binary, PREC_FACTOR},	 // TOKEN_SLASH
	{NULL, NULL, PREC_NONE},		 // TOKEN_SLASH_EQUAL
	{NULL, binary, PREC_FACTOR},	 // TOKEN_STAR
	{NULL, NULL, PREC_NONE},		 // TOKEN_STAR_EQUAL

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
	{NULL, NULL, PREC_NONE},	// TOKEN_SUPER
	{NULL, NULL, PREC_NONE},	// TOKEN_THIS
	{literal, NULL, PREC_NONE}, // TOKEN_TRUE
	{NULL, NULL, PREC_NONE},	// TOKEN_VAR
	{NULL, NULL, PREC_NONE},	// TOKEN_WHILE

	{NULL, NULL, PREC_NONE}, // TOKEN_ERROR
	{NULL, NULL, PREC_NONE}, // TOKEN_EOF
};

static void parsePrecedence(Precedence precedence) {
	advance();
	ParseFn prefixRule = getRule(parser.previous.type)->prefix;
	if (prefixRule == NULL) {
		error("Expect expression.");
		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign);

	while (precedence <= getRule(parser.current.type)->precedence) {
		advance();
		ParseFn infixRule = getRule(parser.previous.type)->infix;
		infixRule(canAssign);
	}

	if (canAssign) {
		if (match(TOKEN_EQUAL) || match(TOKEN_MINUS_EQUAL) || match(TOKEN_PLUS_EQUAL) || match(TOKEN_SLASH_EQUAL) ||
			match(TOKEN_STAR_EQUAL)) {
			error("Invalid assignment target.");
			expression();
		}
	}
}

static uint8_t parseVariable(const char* errorMessage) {
	consume(TOKEN_IDENTIFIER, errorMessage);

	declareVariable();
	if (current->scopeDepth > 0) return 0;

	return identifierConstant(&parser.previous);
}

static void markInitialized() {
	if (current->scopeDepth == 0) return;
	current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
	if (current->scopeDepth > 0) {
		markInitialized();
		return;
	}

	emitBytes(OP_DEFINE_GLOBAL, global);
}

static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

static void block() {
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
	Compiler compiler;
	initCompiler(&compiler, 1, type);

	// Compile the parameter list.
	consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			uint8_t paramConstant = parseVariable("Expect parameter name.");
			defineVariable(paramConstant);

			current->function->arity++;
			if (current->function->arity > 8) {
				error("Cannot have more than 8 parameters.");
			}
		} while (match(TOKEN_COMMA));
	}

	consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

	// The body.
	beginScope();
	if (type == TYPE_ANONYMOUS && match(TOKEN_COLON)) {
		expression();
		emitByte(OP_RETURN);
	} else {
		consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
		block();
	}

	// Create the function object.
	endScope();
	ObjFunction* function = endCompiler();

	// Capture the upvalues in the new closure object.
	emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

	// Emit arguments for each upvalue to know whether to capture a local or an upvalue.
	for (int i = 0; i < function->upvalueCount; i++) {
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(compiler.upvalues[i].index);
	}
}

static void array() {
	uint8_t len = 0;

	while (!match(TOKEN_RIGHT_BRACKET)) {
		if (len > 0) {
			consume(TOKEN_COMMA, "Expect ',' between expressions.");
		}

		expression();
		len++;
	}

	emitBytes(OP_ARRAY, len);
}

static void expression() {
	if (match(TOKEN_FN)) {
		function(TYPE_ANONYMOUS);
	} else if (match(TOKEN_LEFT_BRACKET)) {
		array();
	} else {
		parsePrecedence(PREC_ASSIGNMENT);
	}
}

static void fnDeclaration() {
	uint8_t global = parseVariable("Expect function name.");
	markInitialized();
	function(TYPE_FUNCTION);
	defineVariable(global);
}

static void varDeclaration() {
	uint8_t global = parseVariable("Expect variable name.");

	if (match(TOKEN_EQUAL)) {
		expression();
	} else {
		emitByte(OP_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	defineVariable(global);
}

static void expressionStatement() {
	expression();
	emitByte(OP_POP);
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
}

static void assertStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_ASSERT);
}

static void ifStatement() {
	beginScope();
	expression();
	int ifJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);

	// One-line if notation
	if (match(TOKEN_COLON)) {
		statement();
		endScope();

		int elseJump = emitJump(OP_JUMP);
		patchJump(ifJump);
		emitByte(OP_POP); // Condition
		patchJump(elseJump);
		return;
	}

	consume(TOKEN_LEFT_BRACE, "Expect '{' after if condition.");
	block();
	endScope();

	if (match(TOKEN_ELSE)) {
		int elseJump = emitJump(OP_JUMP);
		patchJump(ifJump);
		emitByte(OP_POP);

		if (match(TOKEN_LEFT_BRACE)) {
			beginScope();
			block();
			endScope();
		} else if (match(TOKEN_IF)) {
			ifStatement();
		} else {
			error("Expect 'if' or '{' after 'else'.");
		}

		patchJump(elseJump);
	} else {
		patchJump(ifJump);
		emitByte(OP_POP);
	}
}

static void returnStatement() {
	if (current->type == TYPE_TOP_LEVEL) {
		error("Cannot return from top-level code.");
	}

	if (match(TOKEN_SEMICOLON)) {
		emitReturn();
	} else {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(OP_RETURN);
	}
}

static void whileStatement() {
	bool inLoop = current->inLoop;
	current->inLoop = true;

	int currentBreak = current->currentBreak;

	int loopStart = currentChunk()->count;

	beginScope();

	expression();

	int exitJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);

	if (match(TOKEN_COLON)) {
		statement();
	} else {
		consume(TOKEN_LEFT_BRACE, "Expect '{' after while condition.");
		block();
	}

	endScope();

	emitLoop(loopStart);

	patchJump(exitJump);
	emitByte(OP_POP);

	if (current->currentBreak != 0) {
		patchJump(current->currentBreak);
	}

	current->inLoop = inLoop;
	current->currentBreak = currentBreak;
}

static void forStatement() {
	bool inLoop = current->inLoop;
	current->inLoop = true;

	int currentBreak = current->currentBreak;

	beginScope();

	// The initialization clause.
	if (match(TOKEN_VAR)) {
		varDeclaration();
	} else if (match(TOKEN_SEMICOLON)) {
		// No initializer.
	} else {
		expressionStatement();
	}

	int loopStart = currentChunk()->count;

	// The exit condition.
	int exitJump = 0;
	if (!match(TOKEN_SEMICOLON)) {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emitJump(OP_JUMP_IF_FALSE);
		emitByte(OP_POP); // Condition
	}

	// Increment step.
	if (!match(TOKEN_LEFT_BRACE)) {
		// We don't want to execute the increment before the body, so jump over it.
		int bodyJump = emitJump(OP_JUMP);

		int incrementStart = currentChunk()->count;
		expression();
		emitByte(OP_POP);

		// After the increment, start the whole loop over.
		emitLoop(loopStart);

		// At the end of the body, we want to jump to the increment, not the top of the loop.
		loopStart = incrementStart;

		patchJump(bodyJump);
	}

	// Compile the body.
	if (match(TOKEN_COLON)) {
		statement();
	} else {
		consume(TOKEN_LEFT_BRACE, "Expect '{' after for clause.");
		beginScope();
		block();
		endScope();
	}

	// Jump back to the beginning (or the increment).
	emitLoop(loopStart);

	if (exitJump != 0) {
		patchJump(exitJump);
		emitByte(OP_POP); // Condition
	}

	if (current->currentBreak != 0) {
		patchJump(current->currentBreak);
	}

	endScope();

	current->inLoop = inLoop;
	current->currentBreak = currentBreak;
}

static void breakStatement() {
	if (current->inLoop == false) {
		error("Break statement cannot be used outside of loop.");
	}

	consume(TOKEN_SEMICOLON, "Expect ';' after break statement.");

	current->currentBreak = emitJump(OP_JUMP);
}

static void synchronize() {
	parser.panicMode = false;

	while (parser.current.type != TOKEN_EOF) {
		if (parser.previous.type == TOKEN_SEMICOLON) return;

		switch (parser.current.type) {
		case TOKEN_CLASS:
		case TOKEN_FN:
		case TOKEN_VAR:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_WHILE:
		case TOKEN_RETURN: return;
		default:; // Do nothing.
		}

		advance();
	}
}

static void declaration() {
	if (match(TOKEN_VAR)) {
		varDeclaration();
	} else if (match(TOKEN_FN)) {
		fnDeclaration();
	} else {
		statement();
	}

	if (parser.panicMode) synchronize();
}

static void statement() {
	if (match(TOKEN_IF)) {
		ifStatement();
	} else if (match(TOKEN_WHILE)) {
		whileStatement();
	} else if (match(TOKEN_FOR)) {
		forStatement();
	} else if (match(TOKEN_BREAK)) {
		breakStatement();
	} else if (match(TOKEN_RETURN)) {
		returnStatement();
	} else if (match(TOKEN_LEFT_BRACE)) {
		beginScope();
		block();
		endScope();
	} else if (match(TOKEN_ASSERT)) {
		assertStatement();
	} else {
		expressionStatement();
	}
}

ObjFunction* compile(const char* source) {
	initScanner(source);
	Compiler compiler;
	initCompiler(&compiler, 0, TYPE_TOP_LEVEL);

	parser.hadError = false;
	parser.panicMode = false;

	advance();

	while (!match(TOKEN_EOF)) {
		declaration();
	}

	ObjFunction* function = endCompiler();

	return parser.hadError ? NULL : function;
}

void grayCompilerRoots() {
	Compiler* compiler = current;
	while (compiler != NULL) {
		grayObject((Obj*)compiler->function);
		compiler = compiler->enclosing;
	}
}
