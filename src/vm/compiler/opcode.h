#ifndef blu_opcode_h
#define blu_opcode_h

typedef enum {
	OP_CONSTANT,
	OP_FALSE,
	OP_NIL,
	OP_TRUE,

	OP_POP,

	OP_EQUAL,
	OP_NOT_EQUAL,
	OP_GREATER,
	OP_GREATER_EQUAL,
	OP_LESS,
	OP_LESS_EQUAL,
	OP_ADD,
	OP_DIVIDE,
	OP_REMINDER,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_NOT,
	OP_NEGATE,

	OP_RETURN,
} bluOpCode;

#endif
