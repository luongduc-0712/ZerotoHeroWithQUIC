CC ?= cc
PKG_CONFIG ?= pkg-config
DEPS_PREFIX ?= $(CURDIR)/.deps
DEPS_PC := $(DEPS_PREFIX)/lib/pkgconfig:$(DEPS_PREFIX)/lib64/pkgconfig
PC := PKG_CONFIG_PATH='$(DEPS_PC):$(PKG_CONFIG_PATH)' $(PKG_CONFIG)
PACKAGES := libngtcp2 libngtcp2_crypto_gnutls gnutls
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Icommon $(shell $(PC) --cflags $(PACKAGES) 2>/dev/null)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDFLAGS += -Wl,-rpath,'$$ORIGIN/../.deps/lib'
LDLIBS += $(shell $(PC) --libs $(PACKAGES) 2>/dev/null)
SERVER_OBJ := $(patsubst %.c,build/%.o,$(wildcard server/*.c))
CLIENT_OBJ := $(patsubst %.c,build/%.o,$(wildcard client/*.c))
COMMON_OBJ := build/common/testbed.o
OBJECTS := $(SERVER_OBJ) $(CLIENT_OBJ) $(COMMON_OBJ)

.PHONY: all check-deps deps certs test clean
all: bin/quic-server bin/quic-client

check-deps:
	@$(PC) --exists 'libngtcp2 >= 1.25.0' 'libngtcp2_crypto_gnutls >= 1.25.0' 'gnutls >= 3.7.2' || \
	  { echo 'Missing dependencies. See README.md; run make deps after installing GnuTLS development tools.'; exit 1; }

$(OBJECTS): | check-deps
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

bin/quic-server: $(SERVER_OBJ) $(COMMON_OBJ)
	@mkdir -p bin
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

bin/quic-client: $(CLIENT_OBJ) $(COMMON_OBJ)
	@mkdir -p bin
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

deps:
	bash scripts/build-deps.sh '$(DEPS_PREFIX)'

certs:
	bash scripts/gen-certs.sh

test: all
	python3 tests/integration.py

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) bin/quic-server bin/quic-client

-include $(OBJECTS:.o=.d)
