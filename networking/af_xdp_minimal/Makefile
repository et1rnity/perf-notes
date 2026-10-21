CC ?= cc
CLANG ?= clang
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra
LIB_CFLAGS = $(shell $(PKG_CONFIG) --cflags libxdp libbpf)
LIBS = $(shell $(PKG_CONFIG) --libs libxdp libbpf)
# Debian/Ubuntu put asm/types.h in the architecture-specific include directory.
MULTIARCH = $(shell $(CC) -dumpmachine)
BPF_CFLAGS ?= -O2 -g -target bpf -Wall -Wextra
BPF_CPPFLAGS ?= -I/usr/include/$(MULTIARCH)

.PHONY: all clean check-deps
all: af_xdp_echo xdp_kern.o

check-deps:
	@$(PKG_CONFIG) --exists libxdp libbpf || { echo "Install libxdp-dev libbpf-dev pkg-config" >&2; exit 1; }

af_xdp_echo: af_xdp_echo.c | check-deps
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIB_CFLAGS) $< -o $@ $(LDFLAGS) $(LIBS)

xdp_kern.o: xdp_kern.c | check-deps
	$(CLANG) $(BPF_CPPFLAGS) $(BPF_CFLAGS) $(LIB_CFLAGS) -c $< -o $@

clean:
	rm -f af_xdp_echo xdp_kern.o
