#include "utils.h"

// From: http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2Float
int64_t bluPowerOf2Ceil(int64_t n) {
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;

	return n;
}
