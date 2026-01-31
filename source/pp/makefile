ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

TARGET = pp
all:
	$(CROSS_COMPILE)gcc $(TARGET).c -o $(TARGET).elf
clean:
	rm -rf $(TARGET).elf