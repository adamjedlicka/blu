#ifndef blu_parser_h
#define blu_parser_h

#include "blu.h"
#include "token.h"

typedef struct {
	const char* source;
	int32_t from;
	int32_t at;
	int32_t lineFrom;
	int32_t lineTo;
	int32_t columnFrom;
	int32_t columnTo;

	bool emitEOF;
} bluParser;

void bluParserInit(bluParser* parser, const char* source);

bluToken bluParserNextToken(bluParser* parser);

#endif
