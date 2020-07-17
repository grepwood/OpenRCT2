#include <cstdint>
#pragma once

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline uint16_t ORCT_bswap16(uint16_t x) {
	return static_cast<uint16_t>((x << 8 ) | (x >> 8));
}

static inline uint32_t ORCT_bswap32(uint32_t x) {
	return static_cast<uint32_t>(((x << 24) | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00) | (x >> 24)));
}

static inline uint64_t ORCT_bswap64(uint64_t x) {
	return static_cast<uint64_t>(	(x >> 56)
				|	(((x) & 0x00ff000000000000ull) >> 40)
				|	(((x) & 0x0000ff0000000000ull) >> 24)
				|	(((x) & 0x000000ff00000000ull) >> 8)
				|	(((x) & 0x00000000ff000000ull) << 8)
				|	(((x) & 0x0000000000ff0000ull) << 24)
				|	(((x) & 0x000000000000ff00ull) << 40)
				|	(x  << 56));
}

#	define ORCT_ensure_value_is_little_endian16(X)	ORCT_bswap16(X)
#	define ORCT_ensure_value_is_little_endian32(X)	ORCT_bswap32(X)
#	define ORCT_ensure_value_is_little_endian64(X)	ORCT_bswap64(X)
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#	define ORCT_ensure_value_is_little_endian16(X)	(X)
#	define ORCT_ensure_value_is_little_endian32(X)	(X)
#	define ORCT_ensure_value_is_little_endian64(X)	(X)
#else
#	error "Middle-endian is not supported. But congrats on getting a PDP-11 or a Honeywell Series 16"
#endif
