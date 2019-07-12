#ifndef blu_parser_h
#define blu_parser_h

#include "token.h"

typedef struct {
	const char* source;
	uint32_t from;
	uint32_t at;
	uint32_t lineFrom;
	uint32_t lineTo;
	uint32_t columnFrom;
	uint32_t columnTo;

	bool emitEOF;
} bluParser;

void bluInitParser(bluParser* parser, const char* source);

bluToken bluNextToken(bluParser* parser);

#endif
