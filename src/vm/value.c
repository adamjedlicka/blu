#include <math.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

void bluInitValueArray(bluVM* vm, bluValueArray* array) {
	array->values = NULL;
	array->capacity = 0;
	array->count = 0;
}

void bluWriteValueArray(bluVM* vm, bluValueArray* array, bluValue value) {
	if (array->capacity < array->count + 1) {
		int oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->values = GROW_ARRAY(vm, array->values, bluValue, oldCapacity, array->capacity);
	}

	array->values[array->count] = value;
	array->count++;
}

void bluFreeValueArray(bluVM* vm, bluValueArray* array) {
	FREE_ARRAY(vm, bluValue, array->values, array->capacity);
}

bool bluValuesEqual(bluVM* vm, bluValue a, bluValue b) {
	if (a.type != b.type) return false;

	switch (a.type) {
	case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
	case VAL_NIL: return true;
	case VAL_NUMBER: return fabs(AS_NUMBER(a) - AS_NUMBER(b)) < __DBL_EPSILON__;
	case VAL_OBJ: return AS_OBJ(a) == AS_OBJ(b);
	default: return false;
	}
}

void bluPrintValue(bluVM* vm, bluValue value) {
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
	case VAL_OBJ: bluPrintObject(vm, value); break;
	}
}
