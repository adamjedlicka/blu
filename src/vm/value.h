#ifndef blu_value_h
#define blu_value_h

#include "include/blu.h"

typedef enum {
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
	VAL_OBJ,
} bluValueType;

typedef struct {
	bluValueType type;
	union {
		bool boolean;
		double number;
		bluObj* obj;
	} as;
} bluValue;

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)

#define BOOL_VAL(value) ((bluValue){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((bluValue){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((bluValue){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(value) ((bluValue){VAL_OBJ, {.obj = (bluObj*)value}})

bool bluValuesEqual(bluValue a, bluValue b);
bool bluIsFalsey(bluValue value);
void bluPrintValue(bluValue value);

#endif
