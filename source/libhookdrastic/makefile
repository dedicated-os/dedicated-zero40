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

# SDL2-only regression tests; no device, ALSA, SDL_image or SDL_ttf required.
SDL2_CONFIG ?= sdl2-config
SDL2_TEST_CFLAGS ?= -I$(shell $(SDL2_CONFIG) --prefix)/include $(shell $(SDL2_CONFIG) --cflags)
SDL2_TEST_LIBS ?= $(shell $(SDL2_CONFIG) --libs)
.PHONY: test-screenframe test-screenframe-portable test-displaymode test-avprofile test-avprofile-detail test-avsystem test-powerstatus test-gametexture test-gameupload
test-screenframe test-screenframe-portable test-displaymode test-avprofile test-avprofile-detail test-avsystem test-powerstatus test-gametexture test-gameupload:
	@set -e; \
		test_binary=$$(mktemp /tmp/drastic-$@.XXXXXX); \
		trap 'rm -f "$$test_binary"' EXIT HUP INT TERM; \
		$(CROSS_COMPILE)gcc $(CPPFLAGS) $(CFLAGS) $(if $(filter %-portable,$@),-DSCREEN_FRAME_DISABLE_NEON) -Wall -Wextra -Werror $(SDL2_TEST_CFLAGS) tests/$(subst -,_,$(patsubst %-portable,%,$@)).c -o "$$test_binary" $(SDL2_TEST_LIBS); \
		$(TEST_RUNNER) "$$test_binary"
