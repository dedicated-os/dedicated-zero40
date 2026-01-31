ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

TARGET = hangmon
all:
	$(CROSS_COMPILE)gcc $(TARGET).c -o $(TARGET)
clean:
	rm -rf $(TARGET)