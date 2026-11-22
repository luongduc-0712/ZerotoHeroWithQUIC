CC ?= cc
PKG_CONFIG ?= pkg-config
# Reuse the existing build; no downloads or changes in the sibling demo.
DEPS_PREFIX ?= $(abspath ../../demo/src/.deps)
DEPS_PC := $(DEPS_PREFIX)/lib/pkgconfig:$(DEPS_PREFIX)/lib64/pkgconfig
PC := PKG_CONFIG_PATH='$(DEPS_PC):$(PKG_CONFIG_PATH)' $(PKG_CONFIG)
PACKAGES := libngtcp2 libngtcp2_crypto_gnutls gnutls
CPPFLAGS += -D_POSIX_C_SOURCE=200809L $(shell $(PC) --cflags $(PACKAGES) 2>/dev/null)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDFLAGS += -Wl,-rpath,'$(DEPS_PREFIX)/lib' -Wl,-rpath,'$(DEPS_PREFIX)/lib64'
LDLIBS += $(shell $(PC) --libs $(PACKAGES) 2>/dev/null)
COMMON := build/app.o build/mqtt_codec.o build/quic_client.o
OBJECTS := $(COMMON) build/publisher.o build/subscriber.o
.PHONY: all check-deps clean test
all: publisher subscriber
check-deps:
	@$(PC) --exists 'libngtcp2 >= 1.25.0' 'libngtcp2_crypto_gnutls >= 1.25.0' 'gnutls >= 3.7.2' || \
	 { echo 'Dependencies missing. Set DEPS_PREFIX to your existing ngtcp2/GnuTLS prefix.'; exit 1; }
$(OBJECTS): | check-deps
build/%.o: %.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
publisher: build/publisher.o $(COMMON)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
subscriber: build/subscriber.o $(COMMON)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
build/test-codec: ../tests/test_codec.c mqtt_codec.c mqtt_codec.h
	@mkdir -p build
	$(CC) -I. -std=c11 $(CFLAGS) ../tests/test_codec.c mqtt_codec.c -o $@
test: build/test-codec
	./build/test-codec
clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) publisher subscriber build/test-codec
-include $(OBJECTS:.o=.d)
