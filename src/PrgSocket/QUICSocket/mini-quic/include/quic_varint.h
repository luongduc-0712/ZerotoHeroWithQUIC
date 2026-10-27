// Chuyen mot so nguyen thanh day byte theo quy tac VARINT cua QUIC va khoi phuc so nguyen ban dau
// Quy tac VARINT cua QUIC nhu sau: hai bit dau(00, 01, 10, 11) voi do dai (1,2,4,8), so bit bieu dien(6, 14,30,62), voi pham vi so nguyen (0-63; 0-16,383; 0-1073741823; 0-2^62-1)

// Header khai bao ham quic_varencode/decode
#ifndef QUIC_VARINT_H //if not define
#define QUIC_VARINT_H


#include <stdint.h>
//Kieu so nguyen dai dien cho kich thuoc cua doi tuong theo bit
#include <stddef.h>

// So nguyen lon nhat ma QUIC bieu dien duoc la 2^62 -1
#define QUIC_VARINT_MAX ((UINT64_C(1) << 62) - UNIT64_C(1))

// So byte can de ma hoa value (1, 2, 4, 8), hoac -1 neu value > QUIC_VARINT_MAX
int quic_varint_size(uint64_t value);

// Gia tri tra ve: 1, 2, 4, 8 (so byte da ghi thanh cong), -1 (loi: vuot gia tri, bo nho khong du...)
int quic_varint_encode(
	// So nguyen can ma hoa
	uint64_t value,
	// So nguyen khong dau 8 bit cho 1 phan, Con tro tro toi vung nhan cac byte sau khi ma hoa
	uint8_t *buffer,
	// Dung luong vung nho ma buffer tro toi de biet con du cho hay khong
	size_t buffer_size
);

int quic_varint_decode(
	// Con tro toi day byte can giai ma
	const uint8_t *buffer,
	// So byte co the doc trong vung nho
	size_t buffer_size,
	// Con tro toi bien nhan gia tri sau khi giai ma
	uint64_t *value
);


#endif
