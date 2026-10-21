CC ?= cc
# Distribution packages may only provide a versioned clang executable.
# Keep explicit CLANG overrides from the environment or command line.
CLANG ?= $(shell for compiler in clang clang-22 clang-21 clang-20 clang-19 clang-18 clang-17 clang-16 clang-15 clang-14; do \
	if command -v $$compiler >/dev/null 2>&1; then echo $$compiler; exit; fi; \
done; echo clang)
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra
LIB_CFLAGS = $(shell $(PKG_CONFIG) --cflags libxdp libbpf)
LIBS = $(shell $(PKG_CONFIG) --libs libxdp libbpf)
# Debian/Ubuntu put asm/types.h in the architecture-specific include directory.
MULTIARCH = $(shell $(CC) -dumpmachine)
BPF_CFLAGS ?= -O2 -g -target bpf -Wall -Wextra
BPF_CPPFLAGS ?= -I/usr/include/$(MULTIARCH)

.PHONY: all clean check-deps check-bpf-deps
all: af_xdp_echo xdp_kern.o

check-deps:
	@command -v $(firstword $(CC)) >/dev/null 2>&1 || { echo "Missing C compiler: $(CC). Install build-essential or set CC." >&2; exit 1; }
	@command -v $(firstword $(PKG_CONFIG)) >/dev/null 2>&1 || { echo "Missing pkg-config. Install pkg-config or set PKG_CONFIG." >&2; exit 1; }
	@$(PKG_CONFIG) --exists libxdp libbpf || { echo "Install libxdp-dev libbpf-dev pkg-config" >&2; exit 1; }

check-bpf-deps: check-deps
	@command -v $(firstword $(CLANG)) >/dev/null 2>&1 || { echo "Missing BPF compiler: $(CLANG). Install clang/LLVM: sudo apt-get install clang llvm; or set CLANG=clang-20 (use an installed version)." >&2; exit 1; }

af_xdp_echo: af_xdp_echo.c | check-deps
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIB_CFLAGS) $< -o $@ $(LDFLAGS) $(LIBS)

xdp_kern.o: xdp_kern.c | check-bpf-deps
	$(CLANG) $(BPF_CPPFLAGS) $(BPF_CFLAGS) $(LIB_CFLAGS) -c $< -o $@

clean:
	rm -f af_xdp_echo xdp_kern.o
