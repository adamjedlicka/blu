#ifndef blu_token_h
#define blu_token_h

#include "blu.h"
#include "token_type.h"

typedef struct {
	bluTokenType type;
	const char* start;
	uint8_t length;
	int32_t line;
	int32_t column;
} bluToken;

#endif
