TARGETS_M0	= $(patsubst %.m0.S,%.swds,$(wildcard *.m0.S))
TARGETS_M3	= $(patsubst %.m3.S,%.swds,$(wildcard *.m3.S))
TARGETS		= $(TARGETS_M0) $(TARGETS_M3)


all: $(TARGETS)

%.swds: %.elf
	arm-none-eabi-objcopy -j.text -j.data  -O binary $< $@

%.elf: %.o
	arm-none-eabi-gcc -e entrypts $< -o $@ -Ttext `echo $@ | sed 's/.*\.\([0-9a-f]*\)\.elf/\1/g'` -nostdlib -mthumb  -Wl,-T linker.lkr

%.o: %.m0.S
	arm-none-eabi-gcc -mcpu=cortex-m0 -c $< -o $@ -mthumb

%.o: %.m3.S
	arm-none-eabi-gcc -mcpu=cortex-m3 -c $< -o $@ -mthumb

clean:
	rm -rf *.o *.swds *.elf