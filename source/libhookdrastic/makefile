ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

TARGET=libhookdrastic
CFLAGS ?= -O2

# Resolve every dependency here so a missing internal function cannot escape
# into the preload library and fail only when DraStic starts rendering.
LDFLAGS += -shared -Wl,--as-needed,--no-undefined
LDLIBS += -lSDL2_image -lSDL2 -lasound -ldl -lpthread

all:
	$(CROSS_COMPILE)gcc $(CPPFLAGS) $(CFLAGS) $(TARGET).c -o $(TARGET).so $(LDFLAGS) -fPIC $(LDLIBS)

# Run on the target architecture (the toolchain container is aarch64), or set
# TEST_RUNNER to an emulator when cross-compiling from another architecture.
.PHONY: test
test:
	@set -e; \
		test_binary=$$(mktemp /tmp/drastic-fastscale-test.XXXXXX); \
		trap 'rm -f "$$test_binary"' EXIT HUP INT TERM; \
		$(CROSS_COMPILE)gcc $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror tests/test_fastscale.c -o "$$test_binary"; \
		$(TEST_RUNNER) "$$test_binary"; \
		$(CROSS_COMPILE)gcc $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror -DFAST_SCALE_DISABLE_NEON tests/test_fastscale.c -o "$$test_binary"; \
		$(TEST_RUNNER) "$$test_binary"; \
		$(CROSS_COMPILE)gcc $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror tests/test_utf8.c -o "$$test_binary"; \
		$(TEST_RUNNER) "$$test_binary"

clean:
	rm -rf $(TARGET).so
