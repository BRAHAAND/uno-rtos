MCU     = atmega328p
F_CPU   = 16000000UL
PORT   ?= /dev/ttyACM0
CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -Wextra -std=gnu99
OBJS    = main.o

all: main.hex

%.o: %.c
	avr-gcc $(CFLAGS) -c $< -o $@

main.elf: $(OBJS)
	avr-gcc -mmcu=$(MCU) $(OBJS) -o $@
	avr-size -C --mcu=$(MCU) $@

main.hex: main.elf
	avr-objcopy -O ihex -R .eeprom $< $@

flash: main.hex
	avrdude -p m328p -c arduino -P $(PORT) -b 115200 -U flash:w:main.hex:i

clean:
	rm -f *.o *.elf *.hex
