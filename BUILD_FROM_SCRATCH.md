# Build uno-rtos from scratch: a beginner's step-by-step guide

This guide walks you through rebuilding the whole kernel yourself, one small step at a time, on an **Arduino Uno R3 with nothing else**: no sensors, no breadboard, no extra parts.

Every step has a **checkpoint tag** in this repo. If you get stuck, compare your code with the tag:

```bash
git clone https://github.com/BRAHAAND/uno-rtos
cd uno-rtos
git diff step4-tcb step5a-cooperative      # what changed between two steps
git show step2-tick:main.c                  # print a step's main.c
git checkout step5b-preemptive              # jump to a working state (git checkout main to return)
```

All tags compile. A few later tags print unused-variable warnings from leftover earlier code; those are harmless.

**You should already know:** basic C (pointers, `struct`, `volatile`), and how to use a terminal. You do **not** need to know assembly, RTOS theory, or the AVR chip; the guide introduces them as needed.

**How to work through it:** type the code yourself rather than copying the repo's `main.c`. You will learn far more, and the tags exist for rescue. If you are stuck on one step for more than 30 minutes, diff against the tag.

---

## Contents

0. [Concepts you need](#0-concepts-you-need)
1. [Setup](#1-setup)
2. [Step 0: bare-metal blink](#step-0-bare-metal-blink)
3. [Step 1: serial output with `printf`](#step-1-serial-output-with-printf)
4. [Step 2: a 1 kHz timer tick](#step-2-a-1-khz-timer-tick)
5. [Step 3: learn how the AVR stack works (no code)](#step-3-learn-how-the-avr-stack-works-no-code)
6. [Step 4: task control blocks and fake stack frames](#step-4-task-control-blocks-and-fake-stack-frames)
7. [Step 5: the context switch](#step-5-the-context-switch)
8. [Step 6: scheduler, priorities, idle task](#step-6-scheduler-priorities-idle-task)
9. [Step 7: `os_delay()`](#step-7-os_delay)
10. [Step 8: semaphores, UART lock, queue](#step-8-semaphores-uart-lock-queue)
11. [Step 9: the full demo with a stack report](#step-9-the-full-demo-with-a-stack-report)
12. [Step 10: measure your kernel](#step-10-measure-your-kernel)
13. [Step 11: priority inversion and priority inheritance](#step-11-priority-inversion-and-priority-inheritance)
14. [Finishing touches](#finishing-touches)
15. [Troubleshooting](#troubleshooting)

---

## 0. Concepts you need

| Term | Meaning |
|---|---|
| **Task** | A function with an endless loop and its own stack, scheduled as if it had the CPU to itself |
| **Stack** | Memory the CPU uses for return addresses and saved registers; each task needs its own |
| **Interrupt** | Hardware makes the CPU pause what it is doing and run a special function (an ISR) |
| **Tick** | A timer interrupt at a fixed rate (here 1000 per second) that gives the kernel control |
| **Context** | Everything needed to resume a task: all CPU registers plus the program counter |
| **Context switch** | Save the running task's context, load another task's context |
| **Preemptive** | The kernel can interrupt a task without its cooperation |
| **TCB** | Task control block: a struct holding a task's saved stack pointer, state, priority, etc. |
| **Priority inversion** | A high-priority task is stuck waiting for a low-priority task, while a medium-priority task runs |

The one idea behind the whole project: **a task that is not running is just a saved stack.** Switching tasks means saving the registers onto the old task's stack, swapping the stack pointer, and restoring registers from the new task's stack.

---

## 1. Setup

You need an Ubuntu (or other Linux) machine. Other operating systems are not covered or tested here; the easiest route is an Ubuntu live USB or dual boot.

```bash
sudo apt install gcc-avr avr-libc avrdude make git
avr-gcc --version        # must print a version
avrdude -? 2>&1 | head -1
```

If `apt` says it can't get a lock, another updater is running; wait a few minutes and retry.

**Find your board.** Plug in the Uno, then:

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Usually it is `/dev/ttyACM0`. Clone boards with a CH340 chip appear as `/dev/ttyUSB0`.

**Serial permission.** Add yourself to the `dialout` group, then **log out and back in**:

```bash
sudo usermod -aG dialout $USER
```

**Test the connection.** This reads the chip's identity without changing anything:

```bash
avrdude -p m328p -c arduino -P /dev/ttyACM0 -b 115200 -v
```

You should see a device signature of `0x1e950f`. If you get `not in sync` or `stk500_recv`, see [Troubleshooting](#troubleshooting).

**Create your project folder:**

```bash
mkdir -p ~/my-rtos && cd ~/my-rtos
git init
```

Commit after every step. A history of small commits is valuable to you and to anyone reading your repo.

---

## Step 0: bare-metal blink

**Goal:** prove that your toolchain works and that you can flash the board, without the Arduino framework.

**New idea:** the LED on pin 13 is bit 5 of port B. `DDRB` sets the direction, and writing a 1 to `PINB` *toggles* the pin on AVR.

`main.c`:

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

Get the Makefile from the checkpoint tag (recipe lines need real tab characters, so don't retype it):

```bash
git clone https://github.com/BRAHAAND/uno-rtos /tmp/uno-rtos-ref
git -C /tmp/uno-rtos-ref show step0-blink:Makefile > Makefile
make
make flash PORT=/dev/ttyACM0     # use /dev/ttyUSB0 for clone boards
```

**Expected:** `make` reports 158 bytes of flash, and the on-board LED blinks once per second.

**Checkpoint tag:** `step0-blink`

---

## Step 1: serial output with `printf`

**Goal:** get text from the board to your terminal. Every later step depends on this for debugging.

**New ideas:**
- The USART0 peripheral sends bytes at a chosen baud rate. At 16 MHz with the `U2X0` double-speed bit set, `UBRR0 = 16` gives 115200 baud.
- avr-libc lets you connect `printf` to your own output function with `FDEV_SETUP_STREAM`.

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

Open the serial terminal (exit with Ctrl-A, then K, then Y):

```bash
screen /dev/ttyACM0 115200
```

**Expected:**

```
hello 0
hello 1
hello 2
...
```

one line every half second, and the LED keeps blinking. If you see garbage, your terminal baud rate is wrong (use 115200).

**Checkpoint tag:** `step1-uart`

---

## Step 2: a 1 kHz timer tick

**Goal:** make the hardware call your code exactly 1000 times per second. This is the heartbeat of the kernel.

**New ideas:**
- Timer1 in **CTC mode** counts from 0 up to `OCR1A`, fires an interrupt, and resets.
- Clock math: 16 MHz ÷ 64 (prescaler) = 250 kHz. The counter runs 0 to 249, which is 250 counts, so 250 kHz ÷ 250 = **1 kHz**. That is why `OCR1A = 249`, not 250.
- `ticks` is `volatile` because an ISR changes it behind your back.
- A `uint16_t` read takes two instructions on an 8-bit CPU, so main code reads it with interrupts briefly disabled.

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

In `main()`, call `timer1_init()` and `sei()` (enable interrupts), then print once per 1000 ticks:

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

**Expected:**

```
tick 1000
tick 2000
tick 3000
...
```

**Verify with a stopwatch:** after 60 seconds you should see about `tick 60000`. If you are off by a lot, recheck the prescaler bits and `OCR1A`.

**Checkpoint tag:** `step2-tick`

---

## Step 3: learn how the AVR stack works (no code)

**Goal:** understand the hardware well enough to write the context switch in Step 5. Skipping this step is the single most common reason people get stuck later.

Using the **ATmega328P datasheet** and the **AVR Instruction Set Manual**, find and write down the answers to these:

1. When an interrupt fires, what does the hardware push, and in what byte order? (On this chip the return address is 2 bytes.)
2. What does `PUSH` do to the stack pointer, and in what order (write then decrement, or decrement then write)?
3. Does the stack pointer point at the last used byte or the next free byte?
4. What does `RETI` do, and how does it differ from `RET`?
5. What is the **SREG** register, and which bit enables interrupts?
6. avr-gcc assumes register **r1** is always zero. Why does that matter when you restore registers?

Then **draw the saved frame on paper**: PC (2 bytes) + r0 + SREG + r1 to r31 = **35 bytes**. You will need this picture in Steps 4 and 5.

**Checkpoint:** you can explain every one of the 35 bytes. There is no tag for this step.

---

## Step 4: task control blocks and fake stack frames

**Goal:** create tasks that exist but are not yet running.

**New ideas:**
- Each task gets a struct (**TCB**) and its own byte array as a stack.
- The TCB's **first member must be the saved stack pointer**, because the assembly in Step 5 reads it at offset 0.
- To start a task for the first time, build a **fake saved context** on its stack so that "restore context" begins executing the task's entry function. The layout, from the top of the stack downward, is:

| What | Value |
|---|---|
| PC low byte, then PC high byte | the task function's address |
| r0 | 0 |
| SREG | `0x80` (interrupts enabled) |
| r1 | 0 |
| r2 ... r31 | 0 |

Paint the unused stack with `0xAA` so you can measure usage later.

```c
typedef struct {
    volatile uint8_t *sp;          /* MUST stay first */
    uint8_t state;
    uint8_t prio;
    uint16_t wake;
    uint8_t stack[STACK_SIZE];
} tcb_t;

/* inside create_task() */
memset(t->stack, 0xAA, STACK_SIZE);
uint8_t *sp = &t->stack[STACK_SIZE - 1];
uint16_t pc = (uint16_t)(uintptr_t)fn;     /* a word address on AVR: exactly what the PC wants */

*sp-- = (uint8_t)(pc & 0xFF);              /* PC low  */
*sp-- = (uint8_t)(pc >> 8);                /* PC high */
*sp-- = 0x00;                              /* r0   */
*sp-- = 0x80;                              /* SREG: I = 1 */
*sp-- = 0x00;                              /* r1   */
for (uint8_t r = 2; r <= 31; r++)
    *sp-- = 0x00;                          /* r2..r31 */
t->sp = sp;
```

**Expected:** nothing visible yet. The checkpoint is that it builds and you can explain why the frame is exactly 35 bytes.

**Checkpoint tag:** `step4-tcb`

---

## Step 5: the context switch

This is the heart of the project. It has two sub-steps.

### Step 5a: cooperative switching (tasks give up the CPU voluntarily)

**Goal:** switch between two tasks when they call `os_yield()`.

**What to write:**

1. **`SAVE_CONTEXT`** (inline assembly macro). In outline:
   ```
   push r0
   in   r0, SREG        ; read SREG BEFORE disabling interrupts
   cli
   push r0              ; saved SREG
   push r1
   clr  r1              ; avr-gcc expects r1 == 0
   push r2 ... push r31
   (store the stack pointer into current->sp)
   ```
2. **`RESTORE_CONTEXT`**: the exact reverse. Load the stack pointer from `current->sp`, pop r31 down to r2, pop r1, pop r0, write it back to SREG, pop r0.
3. **`os_yield()`**: a function with `__attribute__((naked))` so the compiler adds no prologue or epilogue. It runs `SAVE_CONTEXT`, picks the next task (just alternate between two for now), then `RESTORE_CONTEXT`, then returns.
4. **`start_first()`**: a naked function that does only `RESTORE_CONTEXT` followed by `ret`, to launch the first task from its fake frame.

Write these macros yourself, then compare with the exact versions in `git show step5a-cooperative:main.c`.

**Why the order matters:** SREG must be read before `cli`, or you would save "interrupts disabled" and resume tasks with interrupts permanently off.

**Test tasks:** task A prints `A`, waits 200 ms with `_delay_ms`, then calls `os_yield()`. Task B does the same with `B`.

**Expected (from the code; both tasks wait 200 ms then yield):** the letters alternate, one every 200 ms or so: `ABABAB...`

**Checkpoint tag:** `step5a-cooperative`

### Step 5b: preemptive switching (the timer forces the switch)

**Goal:** tasks no longer need to call `os_yield()`; the Timer1 tick interrupt switches them.

**What changes:**
- The Timer1 ISR becomes **naked** and runs: `SAVE_CONTEXT`, call a function that picks the next task, `RESTORE_CONTEXT`, `reti`.
- `os_yield()` now also ends with `reti`, so both paths leave and restore the same frame. This is why one restore sequence serves both.
- Do **not** call `sei()` in `main()`. Task A's fake frame already holds `SREG = 0x80`, so interrupts turn on at exactly the right moment. An early `sei()` could let a tick fire before the first context is loaded and corrupt the first switch.

**Expected (checked in the simulator):** task A prints continuously and task B appears only occasionally, because B waits 200 ms of its own CPU time between prints while sharing the CPU 50/50 with A:

```
AAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAA...
```

Seeing a `B` at all proves preemption works: A never calls `os_yield()`, yet B gets the CPU.

**Checkpoint tag:** `step5b-preemptive`

---

## Step 6: scheduler, priorities, idle task

**Goal:** replace "alternate between two tasks" with a real scheduler.

**What to write:**
- A task table (`tasks[MAX_TASKS]`) and a state per task: `READY` (and later `DELAYED`, `WAITING`).
- `os_schedule()`: find the highest priority among READY tasks, then pick the next READY task *after the current one* at that priority (round-robin among equals).
- An **idle task** at priority 0 that loops forever (`for (;;) {}`). It is always READY, so the scheduler always has something to run.
- `create_task(fn, prio)` now takes a priority.

**Expected:** with tasks A and B both at priority 1, both letters appear regularly. The exact pattern depends on timing.

**Checkpoint tag:** `step6-scheduler`

---

## Step 7: `os_delay()`

**Goal:** let a task sleep without wasting CPU, so lower-priority tasks can run.

**What to write:**
- Add `DELAYED` to the task states and a `wake` time to the TCB.
- `os_delay(n)`: disable interrupts, set `wake = ticks + n`, set the state to `DELAYED`, call `os_yield()`.
- In `os_schedule()`: before choosing, wake every DELAYED task whose time has come:

```c
if (tasks[i].state == T_DELAYED &&
    (int16_t)(ticks - tasks[i].wake) >= 0)
    tasks[i].state = T_READY;
```

**Why the signed cast:** `ticks` is 16 bits and wraps every 65.5 seconds. Subtracting and checking the sign still gives the right answer across the wrap, as long as delays are under 32768 ticks.

**Expected (checked in the simulator):** A and B alternate: `ABABAB...`. Now the CPU is idle between prints. Compare with Step 5a, where `_delay_ms` burned CPU the whole time.

**Checkpoint tag:** `step7-delay`

---

## Step 8: semaphores, UART lock, queue

**Goal:** let tasks wait for each other safely.

**Semaphore:** a counter plus blocking.
- `os_sem_wait(s)`: loop: disable interrupts; if `count > 0`, decrement and return; otherwise mark the task `WAITING` on `s`, call `os_yield()`, and retry when you run again.
- `os_sem_post(s)`: increment the count (with interrupts disabled).
- In `os_schedule()`: a `WAITING` task whose semaphore count is above 0 becomes `READY`.

**UART lock:** a semaphore with initial count 1, taken around every `uart_puts`, so two tasks never interleave characters on the serial line.

**Queue:** a ring buffer plus two semaphores: `items` (starts at 0) and `spaces` (starts at the queue length). `put` waits on `spaces` and posts `items`; `get` does the opposite. This gives blocking behaviour in both directions with no extra scheduler code.

**Demo tasks:** a blink task (`os_delay(500)`), a producer that puts a number into the queue every 250 ms, a consumer that prints what it gets, and a stats task.

**Expected (checked in the simulator):**

```
consumer got 0
consumer got 1
consumer got 2
...
```

one line every 250 ms.

**Checkpoint tag:** `step8-sync-queue`

---

## Step 9: the full demo with a stack report

**Goal:** add a stats task that prints how much stack each task has left, using the `0xAA` paint from Step 4.

```c
uint8_t os_stack_free(uint8_t i)
{
    uint8_t n = 0;
    while (n < STACK_SIZE && tasks[i].stack[n] == 0xAA)
        n++;
    return n;       /* bytes never touched */
}
```

**Expected (checked in the simulator; your numbers may differ by a few bytes):**

```
consumer got 7
consumer got 8
stack free: idle=116 blink=114 prod=114 cons=106 stats=111
consumer got 9
```

Used bytes = 160 minus free. The idle task's 44 used bytes is essentially the interrupt path itself (35-byte frame plus the call into the scheduler), so a rough sizing rule is: **stack needed ≥ deepest call chain + 44 bytes**. These are observed maxima over one run, not guaranteed worst-case bounds.

**Checkpoint tag:** `step9-demo`

---

## Step 10: measure your kernel

**Goal:** turn "it works" into numbers you can put on a resume. Do each measurement on your **real board**.

### 10a. Context-switch time

Timer0 is unused by the kernel, so use it as a stopwatch: free-running with prescaler 8, so 1 count = 0.5 µs = 8 CPU cycles.

- Task A reads `TCNT0` into a global, then calls `os_yield()`.
- Task B reads `TCNT0` as its very first action and prints the difference.
- Subtract a baseline made by reading `TCNT0` twice back to back.

Reference result from this repo: **105 counts = 840 cycles = 52.5 µs**. Yours will be in a similar range but will depend on your task count and scheduler code. This measures the whole yield-to-resume path, so it includes the scheduler's passes over the task table.

**To make your number trustworthy:** repeat the yield 1000 times and report min, median, and max. A single sample can include a tick interrupt that happened to land in the window.

**Checkpoint tag:** `step10a-ctxswitch`

### 10b. Memory footprint

`make` prints it. For the `step9-demo` build the reference is **1996 B flash and 924 B SRAM**; the final v1.0 build is 2276 B and 1052 B. Report your own, with your `avr-gcc --version`.

### 10c. Stack high-water marks

Use the stack report from Step 9 and report used bytes per task.

### 10d. Tick jitter

A task calls `os_delay(10)` 1000 times and records the minimum and maximum tick difference between wake-ups.

Reference stress test from this repo: a higher-priority task spins for about 500 ms, and the measured maximum was **510 ticks**, which is exactly 10 plus 500. That is not "jitter"; it shows that a lower-priority task cannot run while a higher-priority task stays runnable.

**Do both runs:**
1. **Baseline, no competing task.** Expect a maximum of about 10 or 11 ticks.
2. **With a priority-3 busy task.** Expect about 510.

**Checkpoint tag:** `step10d-jitter`

---

## Step 11: priority inversion and priority inheritance

**Goal:** reproduce a classic real-time bug, then fix it.

**Setup:** three tasks, LOW (priority 1), MEDIUM (2), HIGH (3), and one shared lock.

1. LOW takes the lock and does a long piece of work.
2. HIGH wakes up and tries to take the same lock, so it blocks.
3. MEDIUM, which doesn't need the lock, is CPU-bound.

### 11a. Plain lock: inversion

HIGH is waiting for LOW, but MEDIUM outranks LOW, so MEDIUM runs and HIGH waits even longer.

**Expected:**

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

### 11b. Mutex with priority inheritance

**The fix:** when HIGH blocks on a mutex owned by LOW, temporarily raise LOW's priority to HIGH's. Now MEDIUM cannot preempt LOW, so LOW finishes quickly and releases the mutex.

**What to write:**
- A mutex struct with an `owner` pointer, and a `base_prio` field in the TCB next to the effective `prio`.
- `os_mutex_lock`: if the mutex is free, take it. Otherwise raise the owner's `prio` to the caller's if higher, mark the caller `WAITING` on the mutex, yield, and retry when scheduled.
- `os_mutex_unlock`: clear the owner, restore `prio = base_prio`, then yield so a waiting higher-priority task can run.
- `os_schedule()`: a task waiting on a mutex becomes `READY` when the mutex has no owner.

**Expected (checked in the simulator):**

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

Compare the two outputs side by side. That contrast is the whole point of Step 11, and the story to tell in an interview.

**Checkpoint tag:** `demo-inheritance`

---

## Finishing touches

1. **Write your README.** Include a results table with your own measured numbers, the two Step 11 outputs, an architecture diagram, and an honest limitations section. See this repo's README for the structure.
2. **Clean build.** `make clean && make` should print no warnings.
3. **Add a LICENSE.** MIT is the usual choice for learning projects.
4. **Tag your steps** (`git tag step5b-preemptive <hash>`, then `git push --tags`) and a `v1.0`.
5. **Add a short demo video** of the serial output with the LED visible.

---

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `Waiting for cache lock ... dpkg/lock-frontend` | Another updater is running; wait a few minutes and retry |
| `permission denied` on `/dev/ttyACM0` | You are not in the `dialout` group, or you haven't logged out and back in |
| `not in sync` / `stk500_recv()` | Wrong port, a clone board (try `-b 57600`), or something else holds the port (close `screen` and any serial monitor) |
| `make`: `missing separator` | The Makefile lost its tab characters; use `git show step0-blink:Makefile > Makefile` |
| Garbled serial text | Terminal not at 115200 baud, or two tasks printing without the UART lock |
| Board resets or tasks run garbage | Stack overflow: check `os_stack_free()`, raise `STACK_SIZE`, avoid large local arrays in tasks |
| Nothing runs after `start_first()` | The fake frame doesn't match your `SAVE_CONTEXT` push order; recheck against the table in Step 4 |
| Rare crash right at boot | You called `sei()` in `main()` before `start_first()`; remove it |
| Tick timing off by a large factor | Wrong prescaler bits or `OCR1A`: 16 MHz / 64 / 250 = 1000 Hz |
| Interrupts seem permanently off | Some path does `cli()` without a matching `sei()`, or you saved SREG after `cli` instead of before |

If you are still stuck, diff against the nearest tag with `git diff <tag> -- main.c` and look at the first difference.

---

## Where to go next

- Replace the scheduler's four passes over the task table with a single pass, then re-measure the context switch (the biggest cost is probably the scheduler, not the register saves).
- Add software timers, then tickless idle.
- Compute a real stack bound with `-fstack-usage` and right-size `STACK_SIZE`.
- Compare your kernel with FreeRTOS on the same demo.

## References

- Microchip, *ATmega328P Datasheet*
- Microchip, *AVR Instruction Set Manual*
- avr-libc documentation
