#ifndef blu_buffer_h
#define blu_buffer_h

#include "blu.h"
#include "utils.h"

// We need buffers of a few different types. To avoid lots of casting between void* and back, we'll use the preprocessor
// as a poor man's generics and let it generate a few type-specific ones.
#define DECLARE_BUFFER(name, type)                                                                                     \
	typedef struct {                                                                                                   \
		type* data;                                                                                                    \
		int32_t count;                                                                                                 \
		int32_t capacity;                                                                                              \
	} name##Buffer;                                                                                                    \
	void name##BufferInit(name##Buffer* buffer, int32_t size);                                                         \
	void name##BufferFree(name##Buffer* buffer);                                                                       \
	void name##BufferGrow(name##Buffer* buffer, int32_t size);                                                         \
	int32_t name##BufferWrite(name##Buffer* buffer, type data)

// This should be used once for each type instantiation, somewhere in a .c file.
#define DEFINE_BUFFER(name, type)                                                                                      \
	void name##BufferInit(name##Buffer* buffer, int32_t size) {                                                        \
		buffer->data = NULL;                                                                                           \
		buffer->capacity = 0;                                                                                          \
		buffer->count = 0;                                                                                             \
                                                                                                                       \
		if (size > 0) {                                                                                                \
			name##BufferGrow(buffer, bluPowerOf2Ceil(size));                                                           \
		}                                                                                                              \
	}                                                                                                                  \
                                                                                                                       \
	void name##BufferFree(name##Buffer* buffer) {                                                                      \
		if (buffer->data != NULL) free(buffer->data);                                                                  \
		name##BufferInit(buffer, 0);                                                                                   \
	}                                                                                                                  \
                                                                                                                       \
	void name##BufferGrow(name##Buffer* buffer, int32_t size) {                                                        \
		buffer->data = (type*)realloc(buffer->data, size * sizeof(type));                                              \
		buffer->capacity = size;                                                                                       \
	}                                                                                                                  \
                                                                                                                       \
	int32_t name##BufferWrite(name##Buffer* buffer, type data) {                                                       \
		if (buffer->capacity == buffer->count) {                                                                       \
			int32_t capacity = buffer->capacity == 0 ? 8 : buffer->capacity * 2;                                       \
			name##BufferGrow(buffer, capacity);                                                                        \
		}                                                                                                              \
                                                                                                                       \
		buffer->data[buffer->count] = data;                                                                            \
                                                                                                                       \
		return buffer->count++;                                                                                        \
	}

DECLARE_BUFFER(Byte, uint8_t);
DECLARE_BUFFER(Int, int32_t);

#endif
