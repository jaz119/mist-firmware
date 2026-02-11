#BASE ?= /opt/arm-none-eabi/bin/arm-none-eabi
BASE ?= arm-none-eabi

CC      = $(BASE)-gcc
LD      = $(BASE)-gcc
AS      = $(BASE)-as
OBJCOPY = $(BASE)-objcopy
OBJDUMP = $(BASE)-objdump
SIZE    = $(BASE)-size

TODAY = `date +"%m/%d/%y"`

PRJ = firmware
SRC = hw/AT91SAM/Cstartup_SAM7.c hw/AT91SAM/hardware.c hw/AT91SAM/spi.c hw/AT91SAM/mmc.c hw/AT91SAM/at91sam_usb.c hw/AT91SAM/usbdev.c
SRC += fdd.c firmware.c fpga.c hdd.c main.c menu.c menu-minimig.c menu-8bit.c menu_info.c osd.c state.c syscalls.c user_io.c settings.c data_io.c boot.c idxfile.c config.c tos.c ikbd.c xmodem.c ini_parser.c cue_parser.c mist_cfg.c archie.c pcecd.c neocd.c snes.c zx_col.c arc_file.c c64files.c font.c utils.c serial_sink.c
SRC += usb/usb.c usb/max3421e.c usb/usb-max3421e.c usb/usbdebug.c usb/hub.c usb/hid.c usb/hidparser.c usb/xboxusb.c usb/timer.c usb/asix.c usb/pl2303.c usb/storage.c usb/joymapping.c usb/joystick.c
SRC += usb/rtc.c usb/rtc/i2c-tiny.c usb/rtc/i2c-mcp2221.c usb/rtc/pcf85263.c usb/rtc/ds3231.c
SRC += fat_compat.c diskio.c
SRC += FatFs/ff.c FatFs/ffunicode.c
# SRC += usb/storage.c
SRC += cdc_control.c storage_control.c
# SRC += sxmlc/sxmlc.c

OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

LINKMAP  = hw/AT91SAM/AT91SAM7S256-ROM.ld
LIBDIR   =

# Commandline options for each tool.
# for ESA11 add -DEMIST
DFLAGS  = -I. -Iusb -Iarch/ -Ihw/AT91SAM -DCONFIG_ARCH_ARMV4TE -DCONFIG_ARCH_ARM
DFLAGS += -DMIST -DUSB_STORAGE -DFF_FS_TINY=1 -DFF_MAX_LFN=64 -DFF_LFN_BUF=64
CFLAGS  = $(DFLAGS) -march=armv4t -mtune=arm7tdmi -mthumb-interwork -mthumb -fno-common -Os --std=gnu99 -fsigned-char -DVDATE=\"`date +"%y%m%d"`\"
AFLAGS  = -ahls -mapcs-32
LFLAGS  = -mthumb-interwork -mthumb -nostartfiles -Wl,-Map,$(PRJ).map,--cref -T$(LINKMAP) $(LIBDIR)
LFLAGS += --specs=nano.specs
CPFLAGS = --output-target=ihex

MKUPG = mkupg

# Libraries.
LIBS    =

.PRECIOUS: %.d

# Our target.
all: $(PRJ).hex $(PRJ).upg

clean:
	rm -f *.d *.o *.hex *.elf *.map *.lst *.out core *~ */*.d */*.o */*/*.d */*/*.o $(MKUPG) *.bin *.upg *.exe

INTERFACE=interface/ftdi/olimex-arm-usb-tiny-h.cfg
#INTERFACE=interface/busblaster.cfg
#INTERFACE=openocd/interface/esa11-ft4232-generic.cfg
ADAPTER_KHZ=10000

reset:
	openocd -f $(INTERFACE) -f target/at91sam7sx.cfg --command "adapter speed $(ADAPTER_KHZ); init; reset init; resume; shutdown"

$(MKUPG): $(MKUPG).c
	gcc  -o $@ $<

debug: $(PRJ).hex $(PRJ).upg $(PRJ).bin
	openocd -f $(INTERFACE) -f target/at91sam7sx.cfg --command 'adapter speed $(ADAPTER_KHZ); init; reset init; resume; \
	echo "*********************"; echo "Start GDB debug session with:"; echo "> gdb $(PRJ).elf"; echo "(gdb) target ext:3333"; echo "*********************"'

flash: $(PRJ).hex $(PRJ).upg $(PRJ).bin
	openocd -f $(INTERFACE) -f target/at91sam7sx.cfg --command "adapter speed $(ADAPTER_KHZ); init; reset init;  flash protect 0 0 7 off; sleep 1; arm7_9 fast_memory_access enable; flash write_bank 0 $(PRJ).bin 0x0; resume; shutdown"

flash_sam: $(PRJ).hex
	Sam_I_Am -x flash_sam_i_am

# Convert ELF binary to bin file.
$(PRJ).bin: $(PRJ).elf
	@$(OBJCOPY) -O binary $< $@

# Convert ELF binary to Intel HEX file.
$(PRJ).hex: $(PRJ).elf
	@$(OBJCOPY) $(CPFLAGS) $< $@

# Link - this produces an ELF binary.
$(PRJ).elf: crt.o $(OBJ)
	$(LD) $(LFLAGS) -o $@ $+ $(LIBS)
	$(SIZE) -B $@

$(PRJ).upg: $(PRJ).bin $(MKUPG)
	./$(MKUPG) $< $@ `date +"%y%m%d"`

# Compile the C runtime.
crt.o: hw/AT91SAM/Cstartup.S
	$(AS) $(AFLAGS) -o $@ $< > crt.lst

%.o: %.c %.d
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

%.d: ;

firmware.o: CFLAGS += -marm

sections: $(PRJ).elf
	$(OBJDUMP) --section-headers $<

release:
	make $(PRJ).hex $(PRJ).bin $(PRJ).upg
	cp $(PRJ).hex $(PRJ).bin $(PRJ).upg ../bin/firmware

-include $(DEP)
