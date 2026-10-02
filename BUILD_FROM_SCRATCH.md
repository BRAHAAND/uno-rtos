# Build uno-rtos from scratch: a beginner's step-by-step guide

You will build a small preemptive real-time kernel on an **Arduino Uno R3**, one working step at a time, using **only the board and a USB cable**. No sensors, no breadboard, no wiring at any step.

> **What this is, and is not.** This is a *teaching* RTOS built around one chip (ATmega328P), one clock (16 MHz), one tick timer (Timer1) and one UART. The goal is to understand the mechanics (stacks, interrupts, context switches, priorities, locks), not to reimplement FreeRTOS. It is not a general-purpose or production RTOS. Section [Limits](#why-this-tutorial-is-intentionally-limited) lists what is deliberately left out.

Every step has a **checkpoint tag** in the reference repo. If you get stuck, compare your code with the tag:

```bash
git clone https://github.com/BRAHAAND/uno-rtos
cd uno-rtos
git show step2-tick:main.c            # print a step's main.c
git diff step4-tcb step5a-cooperative # see what changed between two steps
git checkout step5b-preemptive        # jump to a working state (git checkout main to return)
```

All tags compile. Some later tags print unused-variable warnings from leftover earlier code; those are harmless.

**You should already know:** basic C (pointers, `struct`, `volatile`) and how to use a terminal. You do **not** need to know assembly, RTOS theory, or the AVR chip; the guide introduces them as needed.

**How to work:** type the code yourself rather than copying the repo's `main.c`. If you are stuck on one step for more than 30 minutes, diff against the tag.

---

## Contents

1. [Bare-metal AVR: what you are actually doing](#1-bare-metal-avr-what-you-are-actually-doing)
2. [Setup](#2-setup)
3. [The starter skeleton](#3-the-starter-skeleton)
4. [AVR cheat sheet](#4-avr-cheat-sheet)
5. [Stack and context diagrams](#5-stack-and-context-diagrams)
6. [How to validate: board first, simulator optional](#6-how-to-validate-board-first-simulator-optional)
7. [Steps 0 to 11](#7-the-steps)
8. [Common beginner mistakes](#8-common-beginner-mistakes)
9. [Troubleshooting](#9-troubleshooting)
10. [Final repo state: how to know you are done](#10-final-repo-state-how-to-know-you-are-done)
11. [Why this tutorial is intentionally limited](#why-this-tutorial-is-intentionally-limited)

---

## 1. Bare-metal AVR: what you are actually doing

**This is not Arduino-sketch programming.** There is no `setup()`, no `loop()`, no `digitalWrite()`, no `delay()`, no `millis()`. You write plain C with a `main()` function and talk to the chip's hardware registers directly.

- **`main.c` is the whole program.** Execution starts at `main()`. Nothing else runs unless you start it.
- **The `Makefile` controls compile and flash.** `make` compiles and links; `make flash` uploads.
- **Why four tools are enough without the Arduino IDE:**
  - `avr-gcc` is the C compiler for AVR chips. (The Arduino IDE calls this same compiler behind the scenes.)
  - `avr-libc` provides the register names (`PORTB`, `UCSR0A`...), `_delay_ms`, `ISR()` and `printf` for AVR.
  - `avrdude` uploads the compiled program. The Uno ships with a small bootloader that listens on the USB serial port, and `avrdude -c arduino` speaks its protocol. (The IDE also calls avrdude.)
  - `make` runs the build commands from the Makefile.
- **No Arduino core is linked.** Since we never include the Arduino library, nothing else touches the timers. We use Timer1 as the kernel tick and Timer0 only as a stopwatch in Step 10.
- **No wiring is needed for any step.** The LED on pin 13 is built into the board, and the USB cable carries both power and serial text.

---

## 2. Setup

You need Ubuntu (or another Linux). Other operating systems are not covered or tested here; the easiest route is an Ubuntu live USB or dual boot.

```bash
sudo apt install gcc-avr avr-libc avrdude make git
avr-gcc --version          # must print a version line
```

If `apt` says it cannot get a lock (`dpkg/lock-frontend`), another updater is running; wait a few minutes and retry.

**Find your board.** Plug in the Uno, then:

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Usually `/dev/ttyACM0`. Clone boards with a CH340 chip appear as `/dev/ttyUSB0`. Use whichever you see wherever this guide says `/dev/ttyACM0`.

**Serial permission.** Add yourself to the `dialout` group, then **log out and back in**:

```bash
sudo usermod -aG dialout $USER
```

**Test the connection.** This reads the chip's identity without changing anything:

```bash
avrdude -p m328p -c arduino -P /dev/ttyACM0 -b 115200 -v
```

You should see a device signature of `0x1e950f`. If you get `not in sync` or `stk500_recv`, see [Troubleshooting](#9-troubleshooting).

---

## 3. The starter skeleton

Create this exact starting state before Step 0:

```bash
mkdir -p ~/my-rtos && cd ~/my-rtos
git init
```

**Project tree at the start:**

```
my-rtos/
├── .gitignore
├── Makefile
└── main.c
```

**`.gitignore`**: keeps build products out of git:

```bash
printf '*.o\n*.elf\n*.hex\n' > .gitignore
```

**`Makefile`**: recipe lines must start with a real TAB character, which copy-paste often destroys, so create it with this command instead of typing it:

```bash
printf 'MCU     = atmega328p\nF_CPU   = 16000000UL\nPORT   ?= /dev/ttyACM0\nCFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -Wextra -std=gnu99\nOBJS    = main.o\n\nall: main.hex\n\n%%.o: %%.c\n\tavr-gcc $(CFLAGS) -c $< -o $@\n\nmain.elf: $(OBJS)\n\tavr-gcc -mmcu=$(MCU) $(OBJS) -o $@\n\tavr-size -C --mcu=$(MCU) $@\n\nmain.hex: main.elf\n\tavr-objcopy -O ihex -R .eeprom $< $@\n\nflash: main.hex\n\tavrdude -p m328p -c arduino -P $(PORT) -b 115200 -U flash:w:main.hex:i\n\nclean:\n\trm -f *.o *.elf *.hex\n' > Makefile
```

(Alternative: clone the reference repo somewhere else and run `git -C <that-clone> show step0-blink:Makefile > Makefile`.)

What the Makefile does, in plain words:

| Target | Command | Meaning |
|---|---|---|
| `make` | compile, link, print size, make `main.hex` | build only |
| `make flash` | build, then run `avrdude` | upload to the board |
| `make flash PORT=/dev/ttyUSB0` | same, other port | for clone boards |
| `make clean` | delete build products | start fresh |

**`main.c`** starts empty; you write it in Step 0. As the project grows, everything stays in this one file (or you may split it later; see the end).

**The cycle you will repeat for every step:**

```bash
# 1. edit main.c
make                           # 2. compile; read the size report and any warnings
make flash PORT=/dev/ttyACM0   # 3. upload
screen /dev/ttyACM0 115200     # 4. watch serial output (exit: Ctrl-A, then K, then Y)
git add -A && git commit -m "Step N: what you did"
```

Close `screen` before running `make flash`; two programs cannot hold the port at once.

---

## 4. AVR cheat sheet

| Term | What it means |
|---|---|
| `DDRx` | Data Direction Register. Bit = 1 means that pin is an output. `DDRB \|= _BV(PB5);` makes pin 13 an output. |
| `PORTx` | Output register. For an output pin, writes the level (1 = high). |
| `PINx` | Input register: reads the pin level. **Writing a 1 to a PINx bit toggles that pin's output** on the ATmega328P, which is why `PINB = _BV(PB5);` flips the LED. |
| `_BV(n)` | `1 << n` |
| `PB5` | bit 5 of port B, which is Arduino pin 13 (the LED) |
| `volatile` | tells the compiler a variable can change behind its back (an ISR changes it), so it must re-read memory every time |
| `ISR(vector)` | defines the function the hardware calls when that interrupt fires |
| `cli()` / `sei()` | disable / enable interrupts globally |
| `SREG` | status register. **Bit 7 (the I flag, value `0x80`) is the global interrupt enable.** |
| `SP` | 16-bit stack pointer (`SPH:SPL`). Points to the **next free byte**; the stack grows toward **lower addresses**. |
| `PUSH r` | write `r` at `[SP]`, then `SP = SP - 1` |
| `POP r` | `SP = SP + 1`, then read `[SP]` into `r` |
| `r0` to `r31` | the 32 CPU registers. avr-gcc assumes **`r1` is always 0** ("zero register") and uses `r0` as scratch. |
| `reti` / `ret` | both pop the return address. `reti` also sets the I flag again (re-enables interrupts). |
| `__attribute__((naked))` | the compiler adds no prologue or epilogue; you control every instruction |
| tick | the periodic Timer1 interrupt (here 1 per millisecond) |
| TCB | task control block: a struct holding a task's saved stack pointer, state, priority |

---

## 5. Stack and context diagrams

### Direction of growth

The stack lives in RAM and grows **downward**. In the project each task owns an array `stack[STACK_SIZE]`; the *top* of the stack is the **highest index**.

```
index 159  ┌─────────┐  <- top of this task's stack (first byte used)
index 158  │         │
   ...     │         │   stack grows toward LOWER indexes
index   0  └─────────┘  <- bottom (if you reach here, you overflowed)
```

### What the hardware and `SAVE_CONTEXT` push, with SP after each step

Starting from an empty stack, `SP` points at `stack[159]` (the next free byte). A tick fires:

| # | Pushed | Written to | SP afterwards |
|---|---|---|---|
| 1 | PC low byte (hardware) | `stack[159]` | `&stack[158]` |
| 2 | PC high byte (hardware) | `stack[158]` | `&stack[157]` |
| 3 | `r0` | `stack[157]` | `&stack[156]` |
| 4 | `SREG` | `stack[156]` | `&stack[155]` |
| 5 | `r1` (then `r1` is cleared) | `stack[155]` | `&stack[154]` |
| 6 | `r2` | `stack[154]` | `&stack[153]` |
| ... | `r3` to `r30` | one byte each, going down | ... |
| 35 | `r31` | `stack[125]` | **`&stack[124]`** (this value is stored in the TCB) |

That is **35 bytes**: 2 (PC) + `r0` + `SREG` + 31 registers (`r1` to `r31`).

### The fake frame that `create_task()` builds

A brand-new task has never run, so there is nothing to restore. We write the same 35 bytes by hand, so that "restore context" starts the task at its entry function:

```
index 159  PC low   <- low byte of the task function's address
index 158  PC high
index 157  r0   = 0
index 156  SREG = 0x80   (interrupt flag set: interrupts will be ON when the task starts)
index 155  r1   = 0
index 154  r2   = 0
   ...     ...
index 125  r31  = 0
index 124  <- t->sp points here (the next free byte)
```

### What `RESTORE_CONTEXT` does (the mirror image)

`POP` increments `SP` first, then reads. So with `SP = &stack[124]`: the first pop reads `stack[125]` (`r31`), and the pops continue upward through `r2`, then `r1`, then the `SREG` slot (written back to `SREG`), then `r0`. Finally `reti` pops the return address, **high byte first, then low byte**, and jumps there. Your fake frame therefore starts the task at its first instruction with interrupts enabled.

### Two ways into the same frame

```
tick interrupt ──► hardware pushes PC ──► SAVE_CONTEXT ──► pick next ──► RESTORE_CONTEXT ──► reti
os_yield() call ─► CALL pushes PC      ──► SAVE_CONTEXT ──► pick next ──► RESTORE_CONTEXT ──► reti
```

Both leave the **same 35-byte frame**, which is why one restore routine serves both.

---

## 6. How to validate: board first, simulator optional

**The board is the source of truth.** For every step, the check is: flash it, open `screen`, and compare with the "Expected" block.

The "Expected" outputs in this guide were produced two ways, and the guide says which:
- **Board** results come from the author's Arduino Uno R3 (the measurements in Step 10).
- **Simulator** results were produced with `simavr`, a free AVR simulator. It is optional, but it lets you check a step without hardware, and it is cycle-accurate enough that its Step 10a result matched the board exactly (105 Timer0 counts = 840 cycles).

**Using `simavr` (optional):**

```bash
sudo apt install simavr
make                                                        # builds main.elf
timeout 10 simavr -m atmega328p -f 16000000 main.elf        # runs it for 10 seconds
```

- Serial output appears in your terminal. You may see stray `..` after each line; ignore them.
- Press Ctrl-C to stop (or use `timeout`).
- There is no LED in the simulator; use the serial output to judge.
- Do not treat it as a replacement for the board: USB, bootloader and real timing quirks are not modelled.

---

## 7. The steps

**Reference sizes** (from the author's code at each tag; your numbers will be close but not identical, since your code will differ in small ways). If your flash number is several times larger, check that `-Os` is in `CFLAGS`.

| Step | Tag | Flash (B) | SRAM (B) |
|---|---|---:|---:|
| 0 | `step0-blink` | 158 | 0 |
| 1 | `step1-uart` | 1764 | 30 |
| 2 | `step2-tick` | 1840 | 32 |
| 4 | `step4-tcb` | 2030 | 216 |
| 5a | `step5a-cooperative` | 714 | 326 |
| 5b | `step5b-preemptive` | 718 | 326 |
| 6 | `step6-scheduler` | 1044 | 495 |
| 7 | `step7-delay` | 1350 | 507 |
| 8 | `step8-sync-queue` | 1764 | 876 |
| 9 | `step9-demo` | 1996 | 924 |
| 11 | `demo-inheritance` (final) | 2276 | 1052 |

### Step 0: bare-metal blink

**Goal:** prove that your toolchain works and that you can flash the board, without the Arduino framework.

**Save this as `main.c`:**

```c
#include <avr/io.h>
#include <util/delay.h>

int main(void)
{
    DDRB |= _BV(PB5);              /* pin 13 (on-board LED) as output */
    for (;;) {
        PINB = _BV(PB5);           /* writing 1 to PINx toggles the pin */
        _delay_ms(500);
    }
}
```

**Commands:**

```bash
make
make flash PORT=/dev/ttyACM0
```

**Expected `make` output** (this is what a healthy build looks like):

```
avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -Os -Wall -Wextra -std=gnu99 -c main.c -o main.o
avr-gcc -mmcu=atmega328p main.o -o main.elf
avr-size -C --mcu=atmega328p main.elf
AVR Memory Usage
----------------
Device: atmega328p

Program:     158 bytes (0.5% Full)
(.text + .data + .bootloader)

Data:          0 bytes (0.0% Full)
(.data + .bss + .noinit)


avr-objcopy -O ihex -R .eeprom main.elf main.hex
```

`Program` is flash used; `Data` is SRAM used (static data only). **`make flash`** should end with `avrdude done.  Thank you.`

**Expected on the board:** the on-board LED blinks once per second.

**Sanity checks:**
- *`make` fails with `avr-gcc: command not found`:* install step in Section 2 did not finish.
- *`make` fails with `missing separator`:* your Makefile lost its tabs; recreate it with the `printf` command in Section 3.
- *Flashing fails:* see Troubleshooting (wrong port, permissions, or another program holding the port).
- *Flash succeeds but no blink:* check you built `main.hex` fresh (`make clean && make flash`). No wiring is involved; the LED is on the board.

**Checkpoint tag:** `step0-blink`

---

### Step 1: serial output with `printf`

**Goal:** get text from the board to your terminal. Every later step depends on this for debugging.

**New ideas:** the USART0 peripheral sends bytes at a chosen baud rate (at 16 MHz with the `U2X0` double-speed bit set, `UBRR0 = 16` gives 115200 baud), and avr-libc lets you connect `printf` to your own output function.

**Replace `main.c` with:**

```c
#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>

static void uart_init(void)
{
    UCSR0A = _BV(U2X0);
    UBRR0  = 16;                          /* 115200 baud @ 16 MHz, U2X */
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);   /* 8N1 */
}

static int uart_putc(char c, FILE *f)
{
    (void)f;
    if (c == '\n') uart_putc('\r', f);
    while (!(UCSR0A & _BV(UDRE0))) { }    /* wait until the transmit buffer is empty */
    UDR0 = c;
    return 0;
}

static FILE uart_out = FDEV_SETUP_STREAM(uart_putc, NULL, _FDEV_SETUP_WRITE);

int main(void)
{
    uint16_t n = 0;
    DDRB |= _BV(PB5);
    uart_init();
    stdout = &uart_out;
    for (;;) {
        PINB = _BV(PB5);
        printf("hello %u\n", n++);
        _delay_ms(500);
    }
}
```

**Commands:**

```bash
make && make flash PORT=/dev/ttyACM0
screen /dev/ttyACM0 115200
```

**Expected build size:** about 1764 B flash and 30 B SRAM.

**Expected serial output:**

```
hello 0
hello 1
hello 2
...
```

One line every half second, and the LED still blinks.

**Sanity checks:**
- *Garbage characters:* the terminal baud is not 115200, or `UBRR0`/`U2X0` were mistyped.
- *Blank terminal, code builds fine:* wrong port in `screen`; or the board wasn't reset after flashing (press the reset button once); or `screen` didn't open (permission, see Setup).
- *`screen` says "Cannot exec" or "Device busy":* another program holds the port; close it.
- *Extra blank lines or staircase text:* the `\r` handling in `uart_putc` is missing.

**Checkpoint tag:** `step1-uart`

---

### Step 2: a 1 kHz timer tick

**Goal:** make the hardware call your code exactly 1000 times per second. This is the heartbeat of the kernel.

**New ideas:**
- Timer1 in **CTC mode** counts from 0 up to `OCR1A`, fires an interrupt, then resets.
- Clock math: 16 MHz ÷ 64 (prescaler) = 250 kHz. The counter runs 0 to 249, which is 250 counts, and 250 kHz ÷ 250 = **1 kHz**. That is why `OCR1A = 249`, not 250.
- `ticks` is `volatile` because an ISR changes it behind your back.
- A `uint16_t` read takes two instructions on an 8-bit CPU, so main code reads it with interrupts briefly disabled.

**Add to your Step 1 code:**

```c
#include <avr/interrupt.h>

static volatile uint16_t ticks;

ISR(TIMER1_COMPA_vect)
{
    ticks++;
}

static void timer1_init(void)
{
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);   /* CTC mode, clk/64 */
    OCR1A  = 249;                                   /* 250 kHz / 250 = 1 kHz */
    TIMSK1 = _BV(OCIE1A);                           /* enable compare-match interrupt */
}
```

**Change `main()`** to call `uart_init()`, set `stdout`, call `timer1_init()`, call `sei()`, and then loop:

```c
uint16_t last = 0;
for (;;) {
    cli();
    uint16_t now = ticks;      /* atomic 16-bit read */
    sei();
    if ((uint16_t)(now - last) >= 1000) {
        last += 1000;
        PINB = _BV(PB5);
        printf("tick %u\n", now);
    }
}
```

**Expected build size:** about 1840 B flash and 32 B SRAM.

**Expected serial output:**

```
tick 1000
tick 2000
tick 3000
...
```

**Verify with a stopwatch:** after 60 seconds you should see about `tick 60000`.

**Sanity checks:**
- *Prints too fast or too slow by a constant factor:* wrong prescaler bits or `OCR1A`; recompute 16 MHz / prescaler / (OCR1A + 1).
- *Nothing prints:* you forgot `sei()`, or `TIMSK1` is not enabled.
- *LED stuck:* the interrupt isn't firing (same causes).
- *Weird counts:* `ticks` was not declared `volatile`.

**Checkpoint tag:** `step2-tick`

---

### Step 3: learn how the AVR stack works (no code)

**Goal:** understand the hardware well enough to write the context switch in Step 5. Skipping this is the most common reason people get stuck later.

Read Sections 4 and 5 above, then, using the **ATmega328P datasheet** and the **AVR Instruction Set Manual**, write down your own answers:

1. When an interrupt fires, what does the hardware push, and in what byte order?
2. What does `PUSH` do to the stack pointer, and in what order?
3. Does `SP` point at the last used byte or the next free byte?
4. What does `RETI` do that `RET` does not?
5. What is `SREG`, and which bit enables interrupts?
6. Why does avr-gcc need `r1` to be zero?

Then **draw the 35-byte frame on paper** without looking at Section 5, and check it afterwards.

**Checkpoint:** you can explain every one of the 35 bytes. There is no tag for this step.

---

### Step 4: task control blocks and fake stack frames

**Goal:** create tasks that exist but are not yet running.

**New ideas:** each task gets a struct (the **TCB**) and its own byte array as a stack. The TCB's **first member must be the saved stack pointer**, because the assembly in Step 5 reads it at offset 0.

**Add (outline; see Section 5 for the layout):**

```c
#define STACK_SIZE 160
#define MAX_TASKS  5

typedef struct {
    volatile uint8_t *sp;          /* MUST stay first */
    uint8_t  state;
    uint8_t  prio;
    uint16_t wake;
    uint8_t  stack[STACK_SIZE];
} tcb_t;

static tcb_t   tasks[MAX_TASKS];
static uint8_t ntasks;
```

And in a `create_task(fn, prio)` function (needs `#include <string.h>`, `<stdint.h>`):

```c
tcb_t *t = &tasks[ntasks];
memset(t->stack, 0xAA, STACK_SIZE);                 /* paint, so you can measure usage later */
uint8_t *sp = &t->stack[STACK_SIZE - 1];
uint16_t pc = (uint16_t)(uintptr_t)fn;              /* a word address on AVR: what the PC expects */

*sp-- = (uint8_t)(pc & 0xFF);                       /* PC low  */
*sp-- = (uint8_t)(pc >> 8);                         /* PC high */
*sp-- = 0x00;                                       /* r0   */
*sp-- = 0x80;                                       /* SREG: I flag set */
*sp-- = 0x00;                                       /* r1   */
for (uint8_t r = 2; r <= 31; r++)
    *sp-- = 0x00;                                   /* r2..r31 */
t->sp = sp;                                         /* next free byte, as in Section 5 */
t->prio = prio;
ntasks++;
```

**Expected:** nothing visible yet. The checkpoint is that it builds (about 2030 B in the author's version, which still carries the UART code) and that you can show on paper that the frame is 35 bytes and `t->sp` ends at `stack[124]`.

**Sanity checks:** *build fails on `memset`:* missing `#include <string.h>`. *Unsure about the layout:* re-read the fake-frame diagram in Section 5.

**Checkpoint tag:** `step4-tcb`

---

### Step 5: the context switch

This is the heart of the project.

#### Step 5a: cooperative switching (tasks give up the CPU voluntarily)

**Goal:** switch between two tasks when they call `os_yield()`.

**What to write:**

1. **`SAVE_CONTEXT`** (inline-assembly macro). In outline:
   ```
   push r0
   in   r0, SREG        ; read SREG BEFORE disabling interrupts
   cli
   push r0              ; this is the saved SREG
   push r1
   clr  r1              ; avr-gcc expects r1 == 0
   push r2 ... push r31
   (store the stack pointer into current->sp)
   ```
2. **`RESTORE_CONTEXT`**: the exact reverse (see Section 5).
3. **`os_yield()`**: a function with `__attribute__((naked))`. It runs `SAVE_CONTEXT`, picks the next task (just alternate between two for now), runs `RESTORE_CONTEXT`, then returns.
4. **`start_first()`**: a naked function that does only `RESTORE_CONTEXT` followed by `ret`, to launch the first task from its fake frame.

Write the macros yourself, then compare with `git show step5a-cooperative:main.c`.

**Test tasks:** task A prints `A`, waits 200 ms with `_delay_ms`, then calls `os_yield()`. Task B does the same with `B`.

**Expected:** the letters alternate about every 200 ms: `ABABAB...` (from the code; checked by reading, not run on the board).

**Sanity checks:**
- *Nothing prints at all:* your first context restore is wrong; check `start_first()` and that the fake frame matches your push order.
- *Prints one letter then freezes:* the saved `SP` isn't stored or reloaded correctly, or the TCB's `sp` isn't its first member.
- *Resets or garbage:* stack pointer direction or the SREG slot is wrong; compare to Section 5.

**Checkpoint tag:** `step5a-cooperative`

#### Step 5b: preemptive switching (the timer forces the switch)

**Goal:** tasks no longer call `os_yield()`; the Timer1 tick switches them.

**What changes:**
- The Timer1 ISR becomes `ISR(TIMER1_COMPA_vect, ISR_NAKED)` and runs `SAVE_CONTEXT`, a call to the function that picks the next task, `RESTORE_CONTEXT`, then `reti`.
- `os_yield()` now also ends with `reti`, so both paths leave and restore the same frame.
- Do **not** call `sei()` in `main()`. Task A's fake frame already holds `SREG = 0x80`, so interrupts turn on exactly when the first context is restored. An early `sei()` could let a tick fire before the first context is loaded and corrupt the first switch.

**Expected (simulator):** task A prints continuously and `B` appears only occasionally, because B waits 200 ms of its *own* CPU time between prints while sharing the CPU 50/50 with A:

```
AAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAA...
```

Seeing any `B` proves preemption works: A never yields.

**Expected build size:** about 718 B flash, 326 B SRAM.

**Sanity checks:** *only A ever prints:* the ISR isn't naked, the tick isn't enabled, or the scheduler always picks the same task. *Random crashes at boot:* check for an early `sei()`.

**Checkpoint tag:** `step5b-preemptive`

---

### Step 6: scheduler, priorities, idle task

**Goal:** replace "alternate between two tasks" with a real scheduler.

**What to write:**
- A state per task: `READY` (later also `DELAYED`, `WAITING`).
- `os_schedule()`: find the highest priority among READY tasks, then pick the next READY task *after the current one* at that priority (round-robin among equals).
- An **idle task** at priority 0 that loops forever (`for (;;) {}`). It is always READY, so the scheduler always has something to run.
- `create_task(fn, prio)` takes a priority.

**Expected:** with tasks A and B at priority 1, both letters appear regularly.

**Expected build size:** about 1044 B flash, 495 B SRAM.

**Sanity checks:** *crash when all tasks block:* you forgot the idle task. *A task never runs:* check your round-robin start index (it must begin after the current task).

**Checkpoint tag:** `step6-scheduler`

---

### Step 7: `os_delay()`

**Goal:** let a task sleep without wasting CPU, so others can run.

**What to write:**
- Add `DELAYED` to the states and a `wake` time to the TCB.
- `os_delay(n)`: disable interrupts, set `wake = ticks + n`, set state `DELAYED`, call `os_yield()`.
- In `os_schedule()`, before choosing, wake every task whose time has come:

```c
if (tasks[i].state == T_DELAYED &&
    (int16_t)(ticks - tasks[i].wake) >= 0)
    tasks[i].state = T_READY;
```

**Why the signed cast:** `ticks` is 16 bits and wraps every 65.5 s. A signed subtraction still gives the right answer across the wrap, as long as delays are under 32768 ticks.

**Expected (simulator):** `ABABAB...`. Now the CPU is idle between prints; Step 5a burned CPU in `_delay_ms`.

**Expected build size:** about 1350 B flash, 507 B SRAM.

**Sanity checks:** *delays far too long or short:* tick rate is wrong (recheck Step 2). *Task never wakes:* `os_schedule()` doesn't run the wake loop before choosing, or the comparison isn't signed.

**Checkpoint tag:** `step7-delay`

---

### Step 8: semaphores, UART lock, queue

**Goal:** let tasks wait for each other safely.

**Semaphore:** a counter plus blocking.
- `os_sem_wait(s)`: loop: disable interrupts; if `count > 0`, decrement and return; otherwise mark the task `WAITING` on `s`, call `os_yield()`, and retry when you run again.
- `os_sem_post(s)`: increment `count` with interrupts disabled.
- In `os_schedule()`: a `WAITING` task whose semaphore count is above 0 becomes `READY`.

**UART lock:** a semaphore with initial count 1, taken around every print, so two tasks never interleave characters.

**Queue:** a ring buffer plus two semaphores: `items` (starts at 0) and `spaces` (starts at the queue length). `put` waits on `spaces` and posts `items`; `get` does the opposite.

**Demo tasks:** blink (`os_delay(500)`), a producer that puts a number into the queue every 250 ms, a consumer that prints it, and a stats task.

**Expected (simulator), one line every 250 ms:**

```
consumer got 0
consumer got 1
consumer got 2
...
```

**Expected build size:** about 1764 B flash, 876 B SRAM.

**Sanity checks:** *interleaved or garbled text:* a print isn't protected by the lock. *Everything freezes:* a task waits on a semaphore nobody posts, or `os_sem_wait` returns without retrying. *Consumer prints nothing:* check the initial counts (`items` 0, `spaces` = length).

**Checkpoint tag:** `step8-sync-queue`

---

### Step 9: the full demo with a stack report

**Goal:** print how much stack each task has left, using the `0xAA` paint from Step 4.

```c
uint8_t os_stack_free(uint8_t i)
{
    uint8_t n = 0;
    while (n < STACK_SIZE && tasks[i].stack[n] == 0xAA)
        n++;
    return n;       /* bytes never touched */
}
```

**Expected (simulator; your numbers may differ by a few bytes):**

```
consumer got 7
consumer got 8
stack free: idle=116 blink=114 prod=114 cons=106 stats=111
consumer got 9
```

Used bytes = 160 minus free. The idle task's 44 used bytes is essentially the interrupt path (35-byte frame plus the call into the scheduler), so a rough sizing rule is **stack needed ≥ deepest call chain + 44 bytes**. These are observed maxima from one run, not guaranteed worst-case bounds.

**Expected build size:** about 1996 B flash, 924 B SRAM.

**Sanity checks:** *free count stays at 160 (or 0):* the paint wasn't applied at creation, or the scan counts from the wrong end. *A task shows 0 free:* it overflowed; raise `STACK_SIZE`.

**Checkpoint tag:** `step9-demo`

---

### Step 10: measure your kernel

**Goal:** turn "it works" into numbers for your README and resume. Run these on the **real board**. Each snippet below is complete code that was compiled and run in `simavr` for this guide.

**How to use a measurement snippet:** keep your kernel (everything from the UART helpers through `start_first()`), delete your demo tasks and old `main()`, paste the snippet below your kernel, `make`, `make flash`, and read the result in `screen`. Commit each measurement separately.

**Helpers both snippets need** (add once):

```c
static void uart_print_u32(uint32_t v)
{
    char buf[10];
    uint8_t n = 0;
    do { buf[n++] = '0' + (uint8_t)(v % 10); v /= 10; } while (v);
    while (n) uart_putc(buf[--n], NULL);
}

static uint16_t os_ticks(void)
{
    cli();
    uint16_t t = ticks;
    sei();
    return t;
}

static void idle_task(void) { for (;;) { } }
```

#### 10a. Context-switch time (1000 samples)

**Idea:** Timer0 is not used by the kernel, so use it as a stopwatch. Free-running with prescaler 8, one count is 0.5 µs, which is 8 CPU cycles. Task A reads `TCNT0` and yields; task B reads `TCNT0` as its first action. The difference, minus a baseline made from two back-to-back reads, is the cost of the whole yield-to-resume path.

```c
#define N_SWITCH 1000

static volatile uint8_t t_start;
static volatile uint8_t baseline;

static void switch_task_a(void)
{
    uint8_t b0 = TCNT0;
    uint8_t b1 = TCNT0;
    baseline = (uint8_t)(b1 - b0);

    for (;;) {
        t_start = TCNT0;
        os_yield();
    }
}

static void switch_task_b(void)
{
    uint8_t  dmin = 255, dmax = 0;
    uint32_t sum = 0;
    uint16_t n = 0;

    for (;;) {
        uint8_t end = TCNT0;                  /* first action after the switch */
        uint8_t d   = (uint8_t)(end - t_start);
        d = (uint8_t)(d - baseline);

        if (d < dmin) dmin = d;
        if (d > dmax) dmax = d;
        sum += d;

        if (++n == N_SWITCH) {
            uart_puts("=== context switch ===\r\n");
            uart_puts("samples = "); uart_print_u32(n);                    uart_puts("\r\n");
            uart_puts("min  = ");    uart_print_u32(dmin);                 uart_puts(" counts = ");
                                     uart_print_u32((uint16_t)dmin * 8);   uart_puts(" cycles\r\n");
            uart_puts("mean = ");    uart_print_u32((sum * 8) / n);        uart_puts(" cycles\r\n");
            uart_puts("max  = ");    uart_print_u32(dmax);                 uart_puts(" counts = ");
                                     uart_print_u32((uint16_t)dmax * 8);   uart_puts(" cycles\r\n");
            for (;;) { }
        }
        os_yield();
    }
}

int main(void)
{
    uart_init();

    TCCR0A = 0;
    TCCR0B = _BV(CS01);        /* Timer0 free-running, clk/8: 1 count = 0.5 us = 8 cycles */
    TCNT0  = 0;

    create_task(idle_task,     0);
    create_task(switch_task_a, 1);
    create_task(switch_task_b, 1);

    os_schedule();
    timer1_init();
    start_first();
}
```

**Reference output** (the final v1.0 kernel, 3 tasks, in `simavr`):

```
=== context switch ===
samples = 1000
min  = 112 counts = 896 cycles
mean = 898 cycles
max  = 115 counts = 920 cycles
```

**Reading the result:**
- Your numbers depend on your scheduler's code and the task count; the cost grows as the scheduler scans more tasks. The repo's recorded **840 cycles** was measured at tag `step10a-ctxswitch`, whose scheduler makes 3 passes over the task table; the final v1.0 scheduler makes 5, which is why the later kernel measures higher. (On the author's board, `step10a-ctxswitch` gave 105 counts = 840 cycles, and `simavr` reproduced exactly that.)
- Resolution is 8 cycles (one Timer0 count), so differences of a count or two are noise.
- A tick interrupt cannot land inside the measured window: `SAVE_CONTEXT` disables interrupts right after saving `SREG`, and they stay off until the next task's `SREG` is restored. So the min/mean/max spread comes from varying scheduler paths and the 8-cycle quantization, not from tick interference.
- Why min/mean/max and not a median: a median needs all 1000 samples stored, which is more SRAM than this chip has to spare.

#### 10b. Memory footprint

`make` prints it; use the `Program` and `Data` lines. For the v1.0 build the author's numbers are **2276 B flash and 1052 B SRAM**. Report yours with `avr-gcc --version`.

#### 10c. Stack high-water marks

Use the stats report from Step 9 and report used bytes (160 minus free) per task.

#### 10d. Tick jitter, baseline (no competing task)

```c
static void uart_print_u16(uint16_t v)
{
    char buf[5];
    uint8_t n = 0;
    do { buf[n++] = '0' + (uint8_t)(v % 10); v /= 10; } while (v);
    while (n) uart_putc(buf[--n], NULL);
}

#define JITTER_SAMPLES 1000

static void jitter_task(void)
{
    uint16_t dmin = 0xFFFF, dmax = 0;
    os_delay(100);                                  /* let the system settle */
    uint16_t previous = os_ticks();

    for (uint16_t i = 0; i < JITTER_SAMPLES; i++) {
        os_delay(10);
        uint16_t now   = os_ticks();
        uint16_t delta = now - previous;
        previous = now;
        if (delta < dmin) dmin = delta;
        if (delta > dmax) dmax = delta;
    }

    uart_puts("=== tick jitter (baseline) ===\r\n");
    uart_puts("samples   = "); uart_print_u16(JITTER_SAMPLES); uart_puts("\r\n");
    uart_puts("min delta = "); uart_print_u16(dmin);           uart_puts(" ticks\r\n");
    uart_puts("max delta = "); uart_print_u16(dmax);           uart_puts(" ticks\r\n");
    for (;;) os_delay(1000);
}

int main(void)
{
    uart_init();
    create_task(idle_task,   0);
    create_task(jitter_task, 1);
    os_schedule();
    timer1_init();
    start_first();
}
```

**Reference output** (`simavr`; the run takes about 10 s of simulated time):

```
=== tick jitter (baseline) ===
samples   = 1000
min delta = 10 ticks
max delta = 10 ticks
```

On hardware, expect 10, and occasionally 11. The resolution is 1 tick (1 ms); sub-millisecond latency is not measured here.

#### 10e. Jitter with interference (the stress test)

Add a priority-3 task that waits 200 ms and then spins for about 500 ms, and give `jitter_task` priority 2:

```c
static void busy_task(void)
{
    os_delay(200);
    uint16_t end = os_ticks() + 500;
    while ((int16_t)(os_ticks() - end) < 0) { }     /* hog the CPU for ~500 ms */
    for (;;) os_delay(1000);
}
```

(In `main()`: `create_task(jitter_task, 2); create_task(busy_task, 3);`.)

The author's board gave **max = 510 ticks**, which is 10 plus the 500 ms the busy task held the CPU. That is not "jitter": it shows that a lower-priority task cannot run while a higher-priority one stays runnable. Report it in your README as "worst-case delay behind a higher-priority busy task", next to the baseline from 10d.

**Checkpoints:** `step10a-ctxswitch`, `step10d-jitter`

---

### Step 11: priority inversion and priority inheritance

**Goal:** reproduce a classic real-time bug, then fix it.

**Setup:** three tasks, LOW (priority 1), MEDIUM (2), HIGH (3), and one shared lock.

1. LOW takes the lock and does a long piece of work.
2. HIGH wakes up and tries to take the same lock, so it blocks.
3. MEDIUM, which doesn't need the lock, is CPU-bound.

#### 11a. Plain lock: inversion

HIGH is waiting for LOW, but MEDIUM outranks LOW, so MEDIUM runs and HIGH waits even longer.

**Expected (simulator):**

```
LOW: starting
LOW: trying to acquire lock
LOW: acquired lock
HIGH: trying to acquire lock
MEDIUM: running              <-- lower priority than HIGH, yet it runs first
LOW: releasing lock
HIGH: acquired lock
```

**Checkpoint tag:** `demo-inversion`

#### 11b. Mutex with priority inheritance

**The fix:** when HIGH blocks on a mutex owned by LOW, temporarily raise LOW's priority to HIGH's. MEDIUM can no longer preempt LOW, so LOW finishes quickly and releases the mutex.

**What to write:**
- A mutex struct with an `owner` pointer, and a `base_prio` field in the TCB next to the effective `prio`.
- `os_mutex_lock`: if the mutex is free, take it. Otherwise raise the owner's `prio` to the caller's if higher, mark the caller `WAITING` on the mutex, yield, and retry when scheduled.
- `os_mutex_unlock`: clear the owner, restore `prio = base_prio`, then yield so a waiting higher-priority task can run.
- `os_schedule()`: a task waiting on a mutex becomes `READY` when the mutex has no owner.

**Expected (simulator):**

```
LOW: starting
LOW: trying to acquire mutex
LOW: acquired mutex
HIGH: trying to acquire mutex
LOW: releasing mutex
HIGH: acquired mutex         <-- HIGH now gets the lock before MEDIUM runs
MEDIUM: running
LOW: priority restored
```

Compare the two outputs side by side. That contrast is the story to tell in an interview.

**Expected build size (final):** about 2276 B flash, 1052 B SRAM.

**Sanity checks:** *HIGH never gets the lock:* the owner's priority is raised but not restored, or the waiting task isn't woken when the owner clears. *MEDIUM still runs first:* the boost isn't applied before the yield.

**Checkpoint tag:** `demo-inheritance`

---

## 8. Common beginner mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Forgetting `volatile` on a variable shared with an ISR (`ticks`) | Loop waits forever, or prints stale values | Declare it `volatile` |
| Reading a 16-bit shared value without disabling interrupts | Rare, random wrong values | Wrap the read in `cli()` / `sei()` |
| `cli()` **before** `in r0, SREG` in `SAVE_CONTEXT` | Tasks resume with interrupts off; the system freezes after the first switch | Read `SREG` first, then `cli` |
| Wrong stack direction (thinking `SP` grows up, or off by one) | Garbage registers, crashes | Re-read the Section 5 tables; `SP` points to the next free byte and decrements on push |
| Not understanding `PINx` toggling | LED never changes, or you try to read the pin | Writing 1 to `PINB` toggles; `PORTB` sets the level |
| Not restoring `r1 = 0` (or never clearing it in the switch) | Mysterious wrong arithmetic in C code after a switch | Save `r1`, `clr r1` after saving, restore it on the way back |
| Wrong `OCR1A` or prescaler math | Tick is not 1 ms; delays and stopwatch disagree | 16 MHz / prescaler / (`OCR1A` + 1) |
| Calling `sei()` in `main()` before `start_first()` | Rare crash at boot | Remove it; the fake frame's `SREG = 0x80` enables interrupts |
| Stack overflow | Resets, or a different task's data gets corrupted | Check `os_stack_free()`; raise `STACK_SIZE`; avoid big local arrays |
| No UART lock | Interleaved or garbled serial lines | Take a semaphore around every print |
| TCB `sp` not the first member | First switch crashes | Keep `sp` first; the assembly reads offset 0 |
| Fake-frame order differs from `SAVE_CONTEXT` push order | First task never starts | The frame must be: PC low, PC high, r0, SREG, r1, r2..r31 (top to bottom) |
| Task function returns | Jumps to a random address | Every task must loop forever |
| Calling blocking functions or printing from an ISR | Deadlock or garbage | Do only tiny work in ISRs |

---

## 9. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `Waiting for cache lock ... dpkg/lock-frontend` | Another updater is running; wait a few minutes and retry |
| `permission denied` on `/dev/ttyACM0` | You are not in the `dialout` group, or you did not log out and back in |
| `not in sync` / `stk500_recv()` | Wrong port, a clone board (try `-b 57600`), or something else holds the port (close `screen`) |
| `make`: `missing separator` | The Makefile lost its tabs; recreate it with the `printf` command in Section 3 |
| Garbled serial text | Terminal not at 115200 baud, or two tasks printing without the UART lock |
| Board resets or tasks run garbage | Stack overflow: check `os_stack_free()`, raise `STACK_SIZE` |
| Nothing runs after `start_first()` | The fake frame doesn't match your `SAVE_CONTEXT` push order |
| Tick timing off by a large factor | Wrong prescaler bits or `OCR1A` |
| Interrupts seem permanently off | A path does `cli()` without a matching `sei()`, or `SREG` was saved after `cli` |
| Builds but prints nothing | Wrong port in `screen`, or press reset after flashing; check that you flashed the new hex |

If you are still stuck, run `git diff <nearest-tag> -- main.c` and look at the first difference.

---

## 10. Final repo state: how to know you are done

By the end of the tutorial your project should look like this:

```
my-rtos/
├── .gitignore       (*.o, *.elf, *.hex)
├── Makefile
├── main.c           kernel, scheduler, semaphores, queue, mutex, demo
├── README.md        results table, demo outputs, diagrams, limitations
└── LICENSE          (MIT is the usual choice)
```

Optional later: split `main.c` into `kernel.c`, `kernel.h`, `uart.c`, `main.c`, and add a `docs/` folder.

**You are done when all of these are true:**
- `make clean && make` prints **no warnings** and the size report is close to the reference (about 2.2 KB flash, 1 KB SRAM).
- The Step 8/9 demo prints the `consumer got N` lines and a stack report.
- Both Step 11 demos print the expected orderings (inversion without the mutex, no inversion with it).
- You have your own measured numbers from the board: context switch (min/mean/max), flash, SRAM, stack usage, and jitter (baseline and stressed).
- Your README states what you measured, how, and what you did **not** measure.
- You have committed after each step, tagged your checkpoints, and pushed to GitHub.

---

## Why this tutorial is intentionally limited

This is a learning kernel. By design it has:

- **One chip, one clock, one timer, one UART:** ATmega328P at 16 MHz, Timer1 for the tick, USART0 for text.
- **No dynamic memory:** no `malloc`; every task stack is a fixed array.
- **No multi-core or SMP:** the chip has one core.
- **No power management:** the idle task spins; there is no sleep or tickless idle.
- **No memory protection:** the chip has no MPU, so a stack overflow can silently corrupt another task.
- **Fixed limits:** 5 tasks including idle, 160-byte stacks.
- **A simple scheduler:** it scans the whole task table every time (O(n)), with no wait queues.
- **Single-level priority inheritance:** no chained inheritance, and recursive locking is not supported.
- **Only the bugs the guide teaches:** it does not cover every preemption hazard a production RTOS must handle.

The goal is to understand the mechanics, not to reimplement FreeRTOS. To go further, see "Where to go next" below.

---

## Where to go next

- Replace the scheduler's multiple passes over the task table with a single pass, then re-measure the context switch. The scheduler's scan, rather than the register saves, is probably the biggest cost.
- Add software timers, then tickless idle.
- Compute a real stack bound with `-fstack-usage` and right-size `STACK_SIZE`.
- Compare your kernel with FreeRTOS on the same demo.

## References

- Microchip, *ATmega328P Datasheet*
- Microchip, *AVR Instruction Set Manual*
- avr-libc documentation
- `simavr` (AVR simulator), for the optional validation workflow
