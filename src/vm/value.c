#include <math.h>

#include "blu.h"
#include "object.h"
#include "value.h"

bool bluValuesEqual(bluValue a, bluValue b) {
	if (a.type != b.type) return false;

	switch (a.type) {
	case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
	case VAL_NIL: return true;
	case VAL_NUMBER: return fabs(AS_NUMBER(a) - AS_NUMBER(b)) < __DBL_EPSILON__;
	case VAL_OBJ: return AS_OBJ(a) == AS_OBJ(b);
	}

	__builtin_unreachable();
}

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
