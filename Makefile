APP				= SWD

COM_FLAGS		= -Wall -Wextra -g -ggdb3
CC_FLAGS		= $(COM_FLAGS) -ffunction-sections -fdata-sections -fomit-frame-pointer -ffreestanding -Wno-unused-but-set-variable -Wno-strict-aliasing -Wno-unused-function -fno-exceptions
LD_FLAGS		= $(COM_FLAGS) -Wl,--gc-sections -Wl,--relax -lm -lc -lgcc

CC_FLAGS-avr	= -Os -I/usr/lib/avr/include -mmcu=attiny85 -mcall-prologues -DF_CPU=16500000UL -DAVR -D_CPU_SUPPORTS_UNALIGNED_ACCESS_ -Wno-aggressive-loop-optimizations -fsplit-wide-types -fno-tree-scev-cprop -finline-limit=3 -fno-inline-small-functions -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums -Wno-pragmas
LD_FLAGS-avr	= -mmcu=attiny85 -e __reset_vector_ptr

CC_FLAGS-efm	= -O2 -mthumb -march=armv6-m -IEFM32HG -I. -DEFM32HG308F64 -DEFM -DCLOCKFREQ=21000000UL -flto
LD_FLAGS-efm	= -O2 -mthumb -march=armv6-m -flto

CC-avr			= avr-gcc
LD-avr			= avr-gcc
OBJCOPY-avr		= avr-objcopy
CC-efm			= arm-none-eabi-gcc
LD-efm			= arm-none-eabi-gcc
OBJCOPY-efm		= arm-none-eabi-objcopy
OBJS			= main.o swd.o memap.o cortex.o
OBJS-avr		= wire-avr-asm.o wire-avr-c.o crt-avr.o plat-avr.o
OBJS-efm		= wire-arm.o crt-efm.o EFM32HG/system_efm32hg.o led-efm.o plat-efm.o blupdate-efm.o codegen.o

#OBJS-efm		+= codegen-run-armv6m.o
OBJS-efm		+= codegen-run-generic.o

OBJS-avr-all	= $(subst .o,.avr.o,$(OBJS) $(OBJS-avr))
OBJS-efm-all	= $(subst .o,.efm.o,$(OBJS) $(OBJS-efm))

all: $(APP).efm.bin $(APP).avr.bin

$(APP).%.bin : $(APP).%.elf Makefile
	$(OBJCOPY-$*)  -j.text -j.data -j.rodata -j.vectors -O binary $< $@

$(APP).avr.elf: $(OBJS-avr-all) avr.lkr Makefile
	$(LD-avr) $(LD_FLAGS) $(LD_FLAGS-avr) -o $@ $(OBJS-avr-all) -Wl,-T avr.lkr

$(APP).efm.elf: $(OBJS-efm-all) efm.lkr Makefile
	$(LD-efm) $(LD_FLAGS) $(LD_FLAGS-efm) -o $@ $(OBJS-efm-all) -Wl,-T efm.lkr

%.efm.o : %.c Makefile
	$(CC-efm) $(CC_FLAGS) $(CC_FLAGS-efm) -c $< -o $@

%.efm.o : %.S Makefile
	$(CC-efm) $(CC_FLAGS) $(CC_FLAGS-efm) -c $< -o $@
	
%.avr.o : %.c Makefile
	$(CC-avr) $(CC_FLAGS) $(CC_FLAGS-avr) -c $< -o $@

%.avr.o : %.S Makefile
	$(CC-avr) $(CC_FLAGS) $(CC_FLAGS-avr) -c $< -o $@

fuses: Makefile
	avrdude -p $(DEVICE) -c avrisp2 -P usb -U efuse:w:0b11111110:m -U hfuse:w:0b11011111:m -U lfuse:w:0b11100001:m -F -B 250
	#no BOD & no HW bootloader
	#no JTAG & OCD, SPI_prog on, WDT off, defaults
	#no clock div, defaults, crystal clock
	
clean: Makefile
	rm -f $(OBJS) $(APP).efm.bin $(APP).avr.bin $(APP).efm.elf $(APP).avr.elf $(OBJS-avr-all) $(OBJS-efm-all)

upload_avr: $(APP).avr.bin Makefile
	sudo ../ModulaR/tool/uploader $<
	rm $<

upload_efm: $(APP).efm.bin Makefile
	../ModulaR/tool/encryptor d1c2c8e7a5bb4ee3c730cb7bb0295f26 < $< > $<.enc
	sudo ../ModulaR/tool/uploader $<.enc
	rm $<

