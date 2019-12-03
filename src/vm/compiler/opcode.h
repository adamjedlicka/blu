#ifndef blu_opcode_h
#define blu_opcode_h

typedef enum {
	OP_CONSTANT,
	OP_FALSE,
	OP_NIL,
	OP_TRUE,
	OP_ARRAY,

	OP_POP,

	OP_GET_LOCAL,
	OP_SET_LOCAL,
	OP_DEFINE_GLOBAL,
	OP_GET_GLOBAL,
	OP_SET_GLOBAL,
	OP_GET_UPVALUE,
	OP_SET_UPVALUE,
	OP_GET_PROPERTY,
	OP_SET_PROPERTY,
	OP_GET_SUPER,
	OP_SUBSCRIPT_GET,
	OP_SUBSCRIPT_SET,

	OP_CALL,
	OP_INVOKE,
	OP_SUPER,
	OP_JUMP,
	OP_JUMP_IF_FALSE,
	OP_JUMP_IF_TRUE,
	OP_LOOP,

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

	OP_CLOSE_OPVALUE,
	OP_CLOSURE,

	OP_CLASS,
	OP_INHERIT,
	OP_METHOD,
	OP_METHOD_FOREIGN,
	OP_METHOD_STATIC,

	OP_IMPORT,

	OP_ECHO,
	OP_RETURN,

	OP_ASSERT,
} bluOpCode;

#endif
