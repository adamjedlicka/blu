#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
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
	PREC_OR,		 // or
	PREC_AND,		 // and
	PREC_EQUALITY,   // == !=
	PREC_COMPARISON, // < > <= >=
	PREC_TERM,		 // + -
	PREC_FACTOR,	 // * /
	PREC_UNARY,		 // ! -
	PREC_CALL,		 // . () []
	PREC_PRIMARY
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
} Local;

typedef struct Compiler {
	Local locals[UINT8_COUNT];
	int localCount;
	int scopeDepth;
	int currentBreak;
} Compiler;

Compiler *current = NULL;

Parser parser;

Chunk *compilingChunk;

static Chunk *currentChunk() {
	return compilingChunk;
}

static void errorAt(Token *token, const char *message) {
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

static void error(const char *message) {
	errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char *message) {
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
static void consume(TokenType type, const char *message) {
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

static void initCompiler(Compiler *compiler) {
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	compiler->currentBreak = 0;
	current = compiler;
}

static void endCompiler() {
	emitReturn();

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError) {
		disassembleChunk(currentChunk(), "code");
	}
#endif
}

static void beginScope() {
	current->scopeDepth++;
}

static void endScope() {
	current->scopeDepth--;

	while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
		emitByte(OP_POP);
		current->localCount--;
	}
}

static void expression();
static void statement();
static void declaration();
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary(bool canAssign) {
	// Remember the operator.
	TokenType operatorType = parser.previous.type;

	// Compile the right operand.
	ParseRule *rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	// Emit the operator instruction.
	switch (operatorType) {
	case TOKEN_BANG_EQUAL: emitBytes(OP_EQUAL, OP_NOT); break;
	case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
	case TOKEN_GREATER: emitByte(OP_GREATER); break;
	case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
	case TOKEN_LESS: emitByte(OP_LESS); break;
	case TOKEN_LESS_EQUAL: emitBytes(OP_GREATER, OP_NOT); break;
	case TOKEN_PLUS: emitByte(OP_ADD); break;
	case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
	case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
	case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
	default: return; // Unreachable.
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

static uint8_t identifierConstant(Token *name) {
	return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token *a, Token *b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name) {
	for (int i = compiler->localCount - 1; i >= 0; i--) {
		Local *local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error("Cannot read local variable in its own initializer.");
			}
			return i;
		}
	}

	return -1;
}

static void addLocal(Token name) {
	if (current->localCount == UINT8_COUNT) {
		error("Too many local variables in function.");
		return;
	}

	Local *local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1;
}

static void declareVariable() {
	// Global variables are implicitly declared.
	if (current->scopeDepth == 0) return;

	Token *name = &parser.previous;
	for (int i = current->localCount - 1; i >= 0; i--) {
		Local *local = &current->locals[i];
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
	} else {
		arg = identifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
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
	{grouping, NULL, PREC_CALL},	 // TOKEN_LEFT_PAREN
	{NULL, NULL, PREC_NONE},		 // TOKEN_RIGHT_PAREN
	{NULL, NULL, PREC_NONE},		 // TOKEN_LEFT_BRACE
	{NULL, NULL, PREC_NONE},		 // TOKEN_RIGHT_BRACE
	{NULL, NULL, PREC_NONE},		 // TOKEN_COMMA
	{NULL, NULL, PREC_CALL},		 // TOKEN_DOT
	{unary, binary, PREC_TERM},		 // TOKEN_MINUS
	{NULL, binary, PREC_TERM},		 // TOKEN_PLUS
	{NULL, NULL, PREC_NONE},		 // TOKEN_SEMICOLON
	{NULL, binary, PREC_FACTOR},	 // TOKEN_SLASH
	{NULL, binary, PREC_FACTOR},	 // TOKEN_STAR
	{unary, NULL, PREC_NONE},		 // TOKEN_BANG
	{NULL, binary, PREC_EQUALITY},   // TOKEN_BANG_EQUAL
	{NULL, NULL, PREC_NONE},		 // TOKEN_EQUAL
	{NULL, binary, PREC_EQUALITY},   // TOKEN_EQUAL_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER
	{NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER_EQUAL
	{NULL, binary, PREC_COMPARISON}, // TOKEN_LESS
	{NULL, binary, PREC_COMPARISON}, // TOKEN_LESS_EQUAL
	{variable, NULL, PREC_NONE},	 // TOKEN_IDENTIFIER
	{string, NULL, PREC_NONE},		 // TOKEN_STRING
	{number, NULL, PREC_NONE},		 // TOKEN_NUMBER
	{NULL, NULL, PREC_AND},			 // TOKEN_AND
	{NULL, NULL, PREC_NONE},		 // TOKEN_BREAK
	{NULL, NULL, PREC_NONE},		 // TOKEN_CLASS
	{NULL, NULL, PREC_NONE},		 // TOKEN_ELSE
	{literal, NULL, PREC_NONE},		 // TOKEN_FALSE
	{NULL, NULL, PREC_NONE},		 // TOKEN_FOR
	{NULL, NULL, PREC_NONE},		 // TOKEN_FUN
	{NULL, NULL, PREC_NONE},		 // TOKEN_IF
	{literal, NULL, PREC_NONE},		 // TOKEN_NIL
	{NULL, NULL, PREC_OR},			 // TOKEN_OR
	{NULL, NULL, PREC_NONE},		 // TOKEN_PRINT
	{NULL, NULL, PREC_NONE},		 // TOKEN_ASSERT
	{NULL, NULL, PREC_NONE},		 // TOKEN_RETURN
	{NULL, NULL, PREC_NONE},		 // TOKEN_SUPER
	{NULL, NULL, PREC_NONE},		 // TOKEN_THIS
	{literal, NULL, PREC_NONE},		 // TOKEN_TRUE
	{NULL, NULL, PREC_NONE},		 // TOKEN_VAR
	{NULL, NULL, PREC_NONE},		 // TOKEN_WHILE
	{NULL, NULL, PREC_NONE},		 // TOKEN_ERROR
	{NULL, NULL, PREC_NONE},		 // TOKEN_EOF
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

	if (canAssign && match(TOKEN_EQUAL)) {
		error("Invalid assignment target.");
		expression();
	}
}

static uint8_t parseVariable(const char *errorMessage) {
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

static ParseRule *getRule(TokenType type) {
	return &rules[type];
}

static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
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

static void printStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
}

static void assertStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_ASSERT);
}

static void ifStatement() {
	beginScope();
	expression();
	int ifJump = emitJump(OP_JUMP_IF);
	consume(TOKEN_LEFT_BRACE, "Expect '{' after if condition.");
	block();
	endScope();

	if (match(TOKEN_ELSE)) {
		int elseJump = emitJump(OP_JUMP);
		patchJump(ifJump);

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
	}
}

static void whileStatement() {
	int currentBreak = current->currentBreak;
	int loopStart = currentChunk()->count;

	beginScope();

	expression();

	int exitJump = emitJump(OP_JUMP_IF);

	consume(TOKEN_LEFT_BRACE, "Expect '{' after while condition.");

	block();

	endScope();

	emitLoop(loopStart);

	patchJump(exitJump);

	if (current->currentBreak != 0) {
		patchJump(current->currentBreak);
	}

	current->currentBreak = currentBreak;
}

static void forStatement() {
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
		exitJump = emitJump(OP_JUMP_IF);
	}

	// Increment step.
	if (!match(TOKEN_LEFT_BRACE)) {
		// We don't want to execute the increment before the body, so jump over it.
		int bodyJump = emitJump(OP_JUMP);

		int incrementStart = currentChunk()->count;
		expression();
		emitByte(OP_POP);
		consume(TOKEN_LEFT_BRACE, "Expect '{' after for clause.");

		// After the increment, start the whole loop over.
		emitLoop(loopStart);

		// At the end of the body, we want to jump to the increment, not the top of the loop.
		loopStart = incrementStart;

		patchJump(bodyJump);
	}

	// Compile the body.
	block();

	// Jump back to the beginning (or the increment).
	emitLoop(loopStart);

	if (exitJump != 0) {
		patchJump(exitJump);
	}

	if (current->currentBreak != 0) {
		patchJump(current->currentBreak);
	}

	endScope();

	current->currentBreak = currentBreak;
}

static void breakStatement() {
	consume(TOKEN_SEMICOLON, "Expect ';' after break statement.");

	current->currentBreak = emitJump(OP_JUMP);
}

static void synchronize() {
	parser.panicMode = false;

	while (parser.current.type != TOKEN_EOF) {
		if (parser.previous.type == TOKEN_SEMICOLON) return;

		switch (parser.current.type) {
		case TOKEN_CLASS:
		case TOKEN_FUN:
		case TOKEN_VAR:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_WHILE:
		case TOKEN_PRINT:
		case TOKEN_RETURN: return;
		default:; // Do nothing.
		}

		advance();
	}
}

static void declaration() {
	if (match(TOKEN_VAR)) {
		varDeclaration();
	} else {
		statement();
	}

	if (parser.panicMode) synchronize();
}

static void statement() {
	if (match(TOKEN_PRINT)) {
		printStatement();
	} else if (match(TOKEN_IF)) {
		ifStatement();
	} else if (match(TOKEN_WHILE)) {
		whileStatement();
	} else if (match(TOKEN_FOR)) {
		forStatement();
	} else if (match(TOKEN_BREAK)) {
		breakStatement();
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

bool compile(const char *source, Chunk *chunk) {
	initScanner(source);
	Compiler compiler;
	initCompiler(&compiler);

	compilingChunk = chunk;
	parser.hadError = false;
	parser.panicMode = false;

	advance();

	while (!match(TOKEN_EOF)) {
		declaration();
	}

	endCompiler();
	return !parser.hadError;
}
