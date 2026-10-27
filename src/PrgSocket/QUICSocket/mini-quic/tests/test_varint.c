#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "quic_varint.h"

int main(void){
	uint8_t buf[8];
	uint64_t in1 = 37;
	uint64_t out;

	// Test encode
	int n = quic_varint_encode(in1, buf, sizeof(buf));
	if (n < 0){
		printf("ERROR");
		return 1;
	}
	printf("So byte su dung = %d\n", n);
	for (int i = 0; i < n; i++){
		printf("Encoded = %02x\n", buf[i]);
	}

	// Test decode
	const uint8_t in[] = {0x9d, 0x7f, 0x3e, 0x7d};
	int n1 = quic_varint_decode(in, sizeof(in), &out);
	if (n1 < 0) {
		printf("ERROR\n");
		return 1;
	} else {
		printf("So byte su dung = %d\n", n1);
		printf("out = %" PRIu64 "\n", out);
	}
	return 0;

}
