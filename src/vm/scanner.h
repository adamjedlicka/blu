#ifndef blu_scanner_h
#define blu_scanner_h

typedef struct {
	const char* start;
	const char* current;
	int line;
	bool emitEOF;
} bluScanner;

typedef enum {
	// Single-character tokens.
	TOKEN_LEFT_PAREN,
	TOKEN_RIGHT_PAREN,
	TOKEN_LEFT_BRACKET,
	TOKEN_RIGHT_BRACKET,
	TOKEN_LEFT_BRACE,
	TOKEN_RIGHT_BRACE,
	TOKEN_COLON,
	TOKEN_COMMA,
	TOKEN_DOT,
	TOKEN_SEMICOLON,
	TOKEN_AT,

	// One or two character tokens.
	TOKEN_BANG,
	TOKEN_BANG_EQUAL,
	TOKEN_EQUAL,
	TOKEN_EQUAL_EQUAL,
	TOKEN_GREATER,
	TOKEN_GREATER_EQUAL,
	TOKEN_LESS,
	TOKEN_LESS_EQUAL,
	TOKEN_MINUS,
	TOKEN_PERCENT,
	TOKEN_PLUS,
	TOKEN_SLASH,
	TOKEN_STAR,

	// Literals.
	TOKEN_IDENTIFIER,
	TOKEN_STRING,
	TOKEN_NUMBER,

	// Keywords.
	TOKEN_AND,
	TOKEN_BREAK,
	TOKEN_CLASS,
	TOKEN_ELSE,
	TOKEN_FALSE,
	TOKEN_FOR,
	TOKEN_FN,
	TOKEN_IF,
	TOKEN_NIL,
	TOKEN_OR,
	TOKEN_ASSERT,
	TOKEN_RETURN,
	TOKEN_SUPER,
	TOKEN_TRUE,
	TOKEN_VAR,
	TOKEN_WHILE,

	TOKEN_ERROR,
	TOKEN_NEWLINE,
	TOKEN_EOF,
} bluTokenType;

typedef struct {
	bluTokenType type;
	const char* start;
	int length;
	int line;
} bluToken;

void bluInitScanner(bluScanner* scanner, const char* source);

bluToken bluScanToken(bluScanner* scanner);

#endif
