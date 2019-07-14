#include "value.h"
#include "blu.h"
#include "object.h"

void bluPrintValue(bluValue value) {
	switch (value.type) {
	case VAL_BOOL: printf(AS_BOOL(value) ? "true" : "false"); break;
	case VAL_NIL: printf("nil"); break;
	case VAL_NUMBER: {
		if (AS_NUMBER(value) == (int)AS_NUMBER(value)) {
			printf("%d", (int)AS_NUMBER(value));
		} else {
			printf("%g", AS_NUMBER(value));
		}
		break;
	}
	case VAL_OBJ: bluPrintObject(value); break;
	}
}
