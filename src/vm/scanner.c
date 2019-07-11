#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

void bluInitScanner(bluScanner* scanner, const char* source) {
	scanner->start = source;
	scanner->current = source;
	scanner->line = 1;
	scanner->emitEOF = false;
}

static bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

static bool isAtEnd(bluScanner* scanner) {
	return *scanner->current == '\0';
}

static char advance(bluScanner* scanner) {
	scanner->current++;
	return scanner->current[-1];
}

static char peek(bluScanner* scanner) {
	return *scanner->current;
}

static char peekNext(bluScanner* scanner) {
	if (isAtEnd(scanner)) return '\0';
	return scanner->current[1];
}

static bool match(bluScanner* scanner, char expected) {
	if (isAtEnd(scanner)) return false;
	if (*scanner->current != expected) return false;

	scanner->current++;
	return true;
}

static bluToken makeToken(bluScanner* scanner, bluTokenType type) {
	bluToken token;
	token.type = type;
	token.start = scanner->start;
	token.length = (int)(scanner->current - scanner->start);
	token.line = scanner->line;

	return token;
}

static bluToken errorToken(bluScanner* scanner, const char* message) {
	bluToken token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (int)strlen(message);
	token.line = scanner->line;

	return token;
}

static void skipWhitespace(bluScanner* scanner) {
	for (;;) {
		char c = peek(scanner);
		switch (c) {
		case ' ':
		case '\r':
		case '\t': advance(scanner); break;
		case '/':
			if (peekNext(scanner) == '/') {
				// A comment goes until the end of the line.
				while (peek(scanner) != '\n' && !isAtEnd(scanner))
					advance(scanner);
			} else {
				return;
			}
			break;
		default: return;
		}
	}
}

static bluToken newline(bluScanner* scanner) {
	scanner->line++;

	return makeToken(scanner, TOKEN_NEWLINE);
}

static bluTokenType checkKeyword(bluScanner* scanner, int start, int length, const char* rest, bluTokenType type) {
	if (scanner->current - scanner->start == start + length && memcmp(scanner->start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_IDENTIFIER;
}

static bluTokenType identifierType(bluScanner* scanner) {
	switch (scanner->start[0]) {
	case 'a':
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'n': return checkKeyword(scanner, 2, 1, "d", TOKEN_AND);
			case 's': return checkKeyword(scanner, 2, 4, "sert", TOKEN_ASSERT);
			}
		}
		break;
	case 'b': return checkKeyword(scanner, 1, 4, "reak", TOKEN_BREAK);
	case 'c': return checkKeyword(scanner, 1, 4, "lass", TOKEN_CLASS);
	case 'e': return checkKeyword(scanner, 1, 3, "lse", TOKEN_ELSE);
	case 'f':
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'a': return checkKeyword(scanner, 2, 3, "lse", TOKEN_FALSE);
			case 'o': return checkKeyword(scanner, 2, 1, "r", TOKEN_FOR);
			case 'n': return checkKeyword(scanner, 2, 0, NULL, TOKEN_FN);
			}
		}
		break;
	case 'i': return checkKeyword(scanner, 1, 1, "f", TOKEN_IF);
	case 'n': return checkKeyword(scanner, 1, 2, "il", TOKEN_NIL);
	case 'o': return checkKeyword(scanner, 1, 1, "r", TOKEN_OR);
	case 'r': return checkKeyword(scanner, 1, 5, "eturn", TOKEN_RETURN);
	case 's': return checkKeyword(scanner, 1, 4, "uper", TOKEN_SUPER);
	case 't':
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'r': return checkKeyword(scanner, 2, 2, "ue", TOKEN_TRUE);
			}
		}
		break;
	case 'v': return checkKeyword(scanner, 1, 2, "ar", TOKEN_VAR);
	case 'w': return checkKeyword(scanner, 1, 4, "hile", TOKEN_WHILE);
	}

	return TOKEN_IDENTIFIER;
}

static bluToken identifier(bluScanner* scanner) {
	while (isAlpha(peek(scanner)) || isDigit(peek(scanner)))
		advance(scanner);

	return makeToken(scanner, identifierType(scanner));
}

static bluToken number(bluScanner* scanner) {
	while (isDigit(peek(scanner)))
		advance(scanner);

	// Look for a fractional part.
	if (peek(scanner) == '.' && isDigit(peekNext(scanner))) {
		// Consume the "."
		advance(scanner);

		while (isDigit(peek(scanner))) {
			advance(scanner);
		}
	}

	return makeToken(scanner, TOKEN_NUMBER);
}

static bluToken string(bluScanner* scanner) {
	while (peek(scanner) != '"' && !isAtEnd(scanner)) {
		if (peek(scanner) == '\n') scanner->line++;
		advance(scanner);
	}

	if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated string.");

	// The closing ".
	advance(scanner);
	return makeToken(scanner, TOKEN_STRING);
}

static bluToken eof(bluScanner* scanner) {
	if (scanner->emitEOF) return makeToken(scanner, TOKEN_EOF);

	scanner->emitEOF = true;

	return makeToken(scanner, TOKEN_NEWLINE);
}

bluToken bluScanToken(bluScanner* scanner) {
	skipWhitespace(scanner);

	scanner->start = scanner->current;

	if (isAtEnd(scanner)) return eof(scanner);

	char c = advance(scanner);
	if (isAlpha(c)) return identifier(scanner);
	if (isDigit(c)) return number(scanner);

	switch (c) {
	case '(': return makeToken(scanner, TOKEN_LEFT_PAREN);
	case ')': return makeToken(scanner, TOKEN_RIGHT_PAREN);
	case '[': return makeToken(scanner, TOKEN_LEFT_BRACKET);
	case ']': return makeToken(scanner, TOKEN_RIGHT_BRACKET);
	case '{': return makeToken(scanner, TOKEN_LEFT_BRACE);
	case '}': return makeToken(scanner, TOKEN_RIGHT_BRACE);
	case ';': return makeToken(scanner, TOKEN_SEMICOLON);
	case '@': return makeToken(scanner, TOKEN_AT);
	case ':': return makeToken(scanner, TOKEN_COLON);
	case ',': return makeToken(scanner, TOKEN_COMMA);
	case '.': return makeToken(scanner, TOKEN_DOT);
	case '-': return makeToken(scanner, TOKEN_MINUS);
	case '+': return makeToken(scanner, TOKEN_PLUS);
	case '/': return makeToken(scanner, TOKEN_SLASH);
	case '*': return makeToken(scanner, TOKEN_STAR);
	case '%': return makeToken(scanner, TOKEN_PERCENT);
	case '!': return makeToken(scanner, match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
	case '=': return makeToken(scanner, match(scanner, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
	case '<': return makeToken(scanner, match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
	case '>': return makeToken(scanner, match(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
	case '"': return string(scanner);
	case '\n': return newline(scanner);
	}

	return errorToken(scanner, "Unexpected character.");
}
