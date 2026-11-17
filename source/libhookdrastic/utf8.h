#ifndef DRASTIC_UTF8_H
#define DRASTIC_UTF8_H

#include <stddef.h>

// Invalid or truncated sequences advance one byte without reading past NUL.
static inline size_t UTF8_charBytes(const unsigned char* p) {
	size_t length;
	if (*p >= 0xC2 && *p <= 0xDF) length = 2;
	else if (*p >= 0xE0 && *p <= 0xEF) length = 3;
	else if (*p >= 0xF0 && *p <= 0xF4) length = 4;
	else return 1;
	for (size_t i = 1; i < length; i++) {
		if ((p[i] & 0xC0) != 0x80) return 1;
	}
	if ((*p == 0xE0 && p[1] < 0xA0) || (*p == 0xED && p[1] >= 0xA0) ||
		(*p == 0xF0 && p[1] < 0x90) || (*p == 0xF4 && p[1] >= 0x90)) return 1;
	return length;
}

#endif
