ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

TARGET=libhookdrastic

all:
	$(CROSS_COMPILE)gcc $(TARGET).c -o $(TARGET).so -shared -lSDL2_image -fPIC
clean:
	rm -rf $(TARGET).so