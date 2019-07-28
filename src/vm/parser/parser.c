#include <string.h>

#include "parser.h"

static bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

static bool isAtEnd(bluParser* parser) {
	return parser->source[parser->at] == '\0';
}

static char advance(bluParser* parser) {
	parser->columnTo++;

	return parser->source[parser->at++];
}

static char peek(bluParser* parser) {
	return parser->source[parser->at];
}

static char peekNext(bluParser* parser) {
	if (isAtEnd(parser)) return '\0';
	return parser->source[parser->at + 1];
}

static bool match(bluParser* parser, char expected) {
	if (isAtEnd(parser)) return false;
	if (peek(parser) != expected) return false;

	advance(parser);

	return true;
}

static bluToken makeToken(bluParser* parser, bluTokenType type) {
	bluToken token;
	token.type = type;

	switch (type) {
	case TOKEN_NEWLINE:
		token.start = "<NEWLINE>";
		token.length = 9;
		break;

	case TOKEN_EOF:
		token.start = "<EOF>";
		token.length = 5;
		break;

	default:
		token.start = parser->source + parser->from;
		token.length = parser->at - parser->from;
		break;
	}

	token.line = parser->lineFrom;
	token.column = parser->columnFrom;

	return token;
}

static bluToken errorToken(bluParser* parser, const char* message) {
	bluToken token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = strlen(message);
	token.line = parser->lineFrom;
	token.column = parser->columnFrom;

	return token;
}

static void skipWhitespace(bluParser* parser) {
	while (true) {
		char c = peek(parser);
		switch (c) {
		case ' ':
		case '\r':
		case '\t': advance(parser); break;
		case '/':
			if (peekNext(parser) == '/') {
				// A comment goes until the end of the line.
				while (peek(parser) != '\n' && !isAtEnd(parser)) {
					advance(parser);
				}
			} else {
				return;
			}
			break;
		default: return;
		}
	}
}

static bluToken newline(bluParser* parser) {
	parser->lineTo++;
	parser->columnTo = 0;

	return makeToken(parser, TOKEN_NEWLINE);
}

static bluTokenType checkKeyword(bluParser* parser, int32_t start, int32_t length, const char* rest,
								 bluTokenType type) {
	if (parser->at - parser->from == start + length &&
		memcmp(parser->source + parser->from + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_IDENTIFIER;
}

static bluTokenType identifierType(bluParser* parser) {
	switch (parser->source[parser->from]) {
	case 'a':
		if (parser->at - parser->from > 1) {
			switch (parser->source[parser->from + 1]) {
			case 'n': return checkKeyword(parser, 2, 1, "d", TOKEN_AND);
			case 's': return checkKeyword(parser, 2, 4, "sert", TOKEN_ASSERT);
			}
		}
		break;
	case 'b': return checkKeyword(parser, 1, 4, "reak", TOKEN_BREAK);
	case 'c': return checkKeyword(parser, 1, 4, "lass", TOKEN_CLASS);
	case 'e':
		if (parser->at - parser->from > 1) {
			switch (parser->source[parser->from + 1]) {
			case 'c': return checkKeyword(parser, 2, 2, "ho", TOKEN_ECHO);
			case 'l': return checkKeyword(parser, 2, 2, "se", TOKEN_ELSE);
			}
		}

		break;
	case 'f':
		if (parser->at - parser->from > 1) {
			switch (parser->source[parser->from + 1]) {
			case 'a': return checkKeyword(parser, 2, 3, "lse", TOKEN_FALSE);
			case 'n': return checkKeyword(parser, 2, 0, NULL, TOKEN_FN);
			case 'o': {
				if (parser->at - parser->from > 2) {
					switch (parser->source[parser->from + 2]) {
					case 'r': {
						if (parser->at - parser->from > 3) {
							switch (parser->source[parser->from + 3]) {
							case 'e': return checkKeyword(parser, 4, 3, "ign", TOKEN_FOREIGN);
							}
						} else {
							return TOKEN_FOR;
						}
						break;
					}
					}
				}
				break;
			}
			}
		}
		break;
	case 'i': return checkKeyword(parser, 1, 1, "f", TOKEN_IF);
	case 'n': return checkKeyword(parser, 1, 2, "il", TOKEN_NIL);
	case 'o': return checkKeyword(parser, 1, 1, "r", TOKEN_OR);
	case 'r': return checkKeyword(parser, 1, 5, "eturn", TOKEN_RETURN);
	case 's': return checkKeyword(parser, 1, 4, "uper", TOKEN_SUPER);
	case 't':
		if (parser->at - parser->from > 1) {
			switch (parser->source[parser->from + 1]) {
			case 'r': return checkKeyword(parser, 2, 2, "ue", TOKEN_TRUE);
			}
		}
		break;
	case 'v': return checkKeyword(parser, 1, 2, "ar", TOKEN_VAR);
	case 'w': return checkKeyword(parser, 1, 4, "hile", TOKEN_WHILE);
	}

	return TOKEN_IDENTIFIER;
}

static bluToken identifier(bluParser* parser) {
	while (isAlpha(peek(parser)) || isDigit(peek(parser))) {
		advance(parser);
	}

	return makeToken(parser, identifierType(parser));
}

static bluToken number(bluParser* parser) {
	while (isDigit(peek(parser))) {
		advance(parser);
	}

	// Look for a fractional part.
	if (peek(parser) == '.' && isDigit(peekNext(parser))) {
		// Consume the "."
		advance(parser);

		while (isDigit(peek(parser))) {
			advance(parser);
		}
	}

	return makeToken(parser, TOKEN_NUMBER);
}

static bluToken string(bluParser* parser) {
	while (peek(parser) != '"' && !isAtEnd(parser)) {
		if (peek(parser) == '\n') {
			newline(parser);
		}

		advance(parser);
	}

	if (isAtEnd(parser)) {
		return errorToken(parser, "Unterminated string.");
	}

	// The closing ".
	advance(parser);

	return makeToken(parser, TOKEN_STRING);
}

static bluToken eof(bluParser* parser) {
	if (parser->emitEOF) {
		return makeToken(parser, TOKEN_EOF);
	}

	parser->emitEOF = true;

	return makeToken(parser, TOKEN_NEWLINE);
}

void bluParserInit(bluParser* parser, const char* source) {
	parser->source = source;
	parser->from = 0;
	parser->at = 0;
	parser->lineFrom = 1;
	parser->lineTo = 1;
	parser->columnFrom = 0;
	parser->columnTo = 0;

	parser->emitEOF = false;
}

bluToken bluParserNextToken(bluParser* parser) {
	skipWhitespace(parser);

	parser->from = parser->at;
	parser->lineFrom = parser->lineTo;
	parser->columnFrom = parser->columnTo;

	if (isAtEnd(parser)) return eof(parser);

	char c = advance(parser);
	if (isAlpha(c)) return identifier(parser);
	if (isDigit(c)) return number(parser);

	switch (c) {
	case '(': return makeToken(parser, TOKEN_LEFT_PAREN);
	case ')': return makeToken(parser, TOKEN_RIGHT_PAREN);
	case '[': return makeToken(parser, TOKEN_LEFT_BRACKET);
	case ']': return makeToken(parser, TOKEN_RIGHT_BRACKET);
	case '{': return makeToken(parser, TOKEN_LEFT_BRACE);
	case '}': return makeToken(parser, TOKEN_RIGHT_BRACE);
	case ';': return makeToken(parser, TOKEN_SEMICOLON);
	case '@': return makeToken(parser, TOKEN_AT);
	case ':': return makeToken(parser, TOKEN_COLON);
	case ',': return makeToken(parser, TOKEN_COMMA);
	case '.': return makeToken(parser, TOKEN_DOT);
	case '-': return makeToken(parser, TOKEN_MINUS);
	case '+': return makeToken(parser, TOKEN_PLUS);
	case '/': return makeToken(parser, TOKEN_SLASH);
	case '*': return makeToken(parser, TOKEN_STAR);
	case '%': return makeToken(parser, TOKEN_PERCENT);
	case '!': return makeToken(parser, match(parser, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
	case '=': return makeToken(parser, match(parser, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
	case '<': return makeToken(parser, match(parser, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
	case '>': return makeToken(parser, match(parser, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
	case '"': return string(parser);
	case '\n': return newline(parser);
	}

	return errorToken(parser, "Unexpected character.");
}
