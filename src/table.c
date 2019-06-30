#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

void freeTable(Table* table) {
	FREE_ARRAY(Entry, table->entries, table->capacity);
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
	uint32_t index = key->hash % capacity;
	Entry* tombstone = NULL;

	for (;;) {
		Entry* entry = &entries[index];

		if (entry->key == NULL) {
			if (IS_NIL(entry->value)) {
				// Empty entry.
				return tombstone != NULL ? tombstone : entry;
			} else {
				// We found a tombstone.
				if (tombstone == NULL) tombstone = entry;
			}
		} else if (entry->key == key) {
			// We found the key.
			return entry;
		}

		index = (index + 1) % capacity;
	}
}

bool tableGet(Table* table, ObjString* key, Value* value) {
	if (table->entries == NULL) return false;

	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL) return false;

	*value = entry->value;
	return true;
}

static void adjustCapacity(Table* table, int capacity) {
	Entry* entries = ALLOCATE(Entry, capacity);

	for (int i = 0; i < capacity; i++) {
		entries[i].key = NULL;
		entries[i].value = NIL_VAL;
	}

	table->count = 0;
	for (int i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		if (entry->key == NULL) continue;

		Entry* dest = findEntry(entries, capacity, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->count++;
	}

	FREE_ARRAY(Entry, table->entries, table->capacity);

	table->entries = entries;
	table->capacity = capacity;
}

bool tableSet(Table* table, ObjString* key, Value value) {
	if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
		int capacity = GROW_CAPACITY(table->capacity);
		adjustCapacity(table, capacity);
	}

	Entry* entry = findEntry(table->entries, table->capacity, key);

	bool isNewKey = entry->key == NULL;
	// Increase count only if we are inserting into empty bucket, not when replacing tombstone
	if (isNewKey && IS_NIL(entry->value)) table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
	if (table->count == 0) return false;

	// Find the entry.
	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL) return false;

	// Place a tombstone in the entry.
	entry->key = NULL;
	entry->value = BOOL_VAL(true);

	return true;
}

void tableAddAll(Table* from, Table* to) {
	for (int i = 0; i < from->capacity; i++) {
		Entry* entry = &from->entries[i];
		if (entry->key != NULL) {
			tableSet(to, entry->key, entry->value);
		}
	}
}

// Function tableGet uses findEntry which compares keys with double equals,
// so if they are in the same place in memory.
// For string interning we want to compare keys fully with memcmp.
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
	// If the table is empty, we definitely won't find it.
	if (table->entries == NULL) return NULL;

	uint32_t index = hash % table->capacity;

	for (;;) {
		Entry* entry = &table->entries[index];

		if (entry->key == NULL) {
			// Stop if we find an empty non-tombstone entry.
			if (IS_NIL(entry->value)) return NULL;
		} else if (entry->key->length == length && entry->key->hash == hash &&
				   memcmp(entry->key->chars, chars, length) == 0) {
			// We found it.
			return entry->key;
		}

		// Try the next slot.
		index = (index + 1) % table->capacity;
	}
}

void tableRemoveWhite(Table* table) {
	for (int i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		if (entry->key != NULL && !entry->key->obj.isDark) {
			tableDelete(table, entry->key);
		}
	}
}

void grayTable(Table* table) {
	for (int i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		grayObject((Obj*)entry->key);
		grayValue(entry->value);
	}
}
