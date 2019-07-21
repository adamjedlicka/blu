#ifndef blu_opcode_h
#define blu_opcode_h

typedef enum {
	OP_CONSTANT,
	OP_FALSE,
	OP_NIL,
	OP_TRUE,

	OP_POP,

	OP_EQUAL,
	OP_ADD,
	OP_DIVIDE,
	OP_REMINDER,
	OP_SUBTRACT,
	OP_MULTIPLY,

	OP_RETURN,
} bluOpCode;

#endif
