# Preemptive RTOS from scratch on Arduino Uno R3 (ATmega328P)

A learning project: a preemptive RTOS kernel written in C and AVR assembly,
with no Arduino framework and no FreeRTOS.

**Status: work in progress.**

Done so far:
- Bare-metal blink and UART `printf` (115200 baud)
- Timer1 1 kHz system tick
- Task control blocks with fake initial stack frames
- Context switch in AVR assembly (35-byte frame), cooperative and timer-driven

Planned: priority scheduler, delays, semaphores, queues, priority inheritance.

## Build and flash (Linux)
    sudo apt install gcc-avr avr-libc avrdude make
    make
    avrdude -p m328p -c arduino -P /dev/ttyACM0 -b 115200 -U flash:w:main.hex:i
