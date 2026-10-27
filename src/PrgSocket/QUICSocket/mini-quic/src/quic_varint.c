#include <stdio.h>
#include <string.h>
#include "quic_varint.h"

int quic_varint_size(uint64_t value)
{
	if (value <= (UINT64_C(1) << 6) - 1) return 1;
	if (value <= (UINT64_C(1) << 14) - 1) return 2;
	if (value <= (UINT64_C(1) << 30) - 1) return 4;
	if (value <= (UINT64_C(1) << 62) - 1) return 8;
	printf("Value haven't valid");
	return -1;
}

int quic_varint_encode(uint64_t value, uint8_t *buffer, size_t buffer_size)
{
	// Kiem tra buffer
	if (buffer == NULL){
		printf("Buffer not already");
		return -1;
	}

	// Kiem tra value
	size_t len = quic_varint_size(value);

	// Kiem tra khong gian cua Buffer
	if (buffer_size < len) {
		printf("Space of Buffer not enough");
		return -1;
	}

	// Them 2 bit dia chi do dai
	uint64_t encoded = value;

	if (len == 2) {
		encoded |= (UINT64_C(1) << 14);
	}
	if (len == 4) {
		encoded |= (UINT64_C(2) << 30);
	}
	if(len == 8) {
		encoded |= (UINT64_C(3) << 62);
	}

	// Tach cac byte va ghi theo big-endien
	for (size_t i = 0; i < len; i++){
		size_t shift = 8 * (len - 1 - i);
		buffer[i] = (uint8_t)((encoded << shift) & 0xFF);
	}

	return (int) len;
}

int quic_varint_decode(const uint8_t *buffer, size_t buffer_size, uint64_t *value)
{
	// Kiem tra buffer, value, buffer_size
	if (buffer == NULL || value == NULL || buffer_size == 0) {
		return -1;
	}

	// Xac dinh do dai tu 2 bit cao dau tien
	size_t len = (size_t)1 << (buffer[0] >> 6);

	// Kiem tra bo nho co du byte hay khong
	if (buffer_size < len) {
		return -1;
	}

	// Lay 6 bit gia tri dau tien, bo 2 bit dia chi do dai
	uint64_t decoded = buffer[0] & 0x3F;

	// Doc cac byte con lai va ghep thanh gia tri
	for (size_t i = 1; i < len; i++){
		decoded = (decoded << 8) | buffer[i];
	}

	// Ghi ket qua
	*value = decoded;
	return (int)len;
}





















