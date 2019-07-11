#ifndef blu_table_h
#define blu_table_h

#include "blu.h"
#include "common.h"
#include "object.h"
#include "value.h"

typedef struct {
	bluObjString* key;
	bluValue value;
} bluEntry;

typedef struct {
	int count;
	int capacityMask;
	bluEntry* entries;
} bluTable;

void bluInitTable(bluVM* vm, bluTable* table);
void bluFreeTable(bluVM* vm, bluTable* table);

bool bluTableGet(bluVM* vm, bluTable* table, bluObjString* key, bluValue* value);
bool bluTableSet(bluVM* vm, bluTable* table, bluObjString* key, bluValue value);
bool bluTableDelete(bluVM* vm, bluTable* table, bluObjString* key);
void bluTableAddAll(bluVM* vm, bluTable* from, bluTable* to);
bluObjString* bluTableFindString(bluVM* vm, bluTable* table, const char* chars, int length, uint32_t hash);

void bluTableRemoveWhite(bluVM* vm, bluTable* table);
void bluGrayTable(bluVM* vm, bluTable* table);

#endif
