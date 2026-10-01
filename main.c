#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>
#include <avr/interrupt.h>

/* =========================================================
 * UART
 * ========================================================= */

static void uart_init(void)
{
    UCSR0A = _BV(U2X0);
    UBRR0  = 16;                         // 115200 baud @ 16 MHz, U2X
    UCSR0B = _BV(TXEN0);                 // Enable transmitter
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00); // 8N1
}

static int uart_putc(char c, void *unused)
{
    (void)unused;

    while (!(UCSR0A & _BV(UDRE0)))
        ;

    UDR0 = c;

    return 0;
}


/* =========================================================
 * TIMER1
 *
 * 16 MHz / 64 = 250 kHz
 *
 * OCR1A = 249
 *
 * 250 kHz / (249 + 1) = 1000 Hz
 *
 * Therefore:
 *      Timer interrupt every 1 ms
 * ========================================================= */

static void timer1_init(void)
{
    TCCR1A = 0;

    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);

    OCR1A = 249;

    TIMSK1 = _BV(OCIE1A);
}


/* =========================================================
 * TCB + TASK STACKS
 * ========================================================= */

#define STACK_SIZE 160

typedef struct {
    volatile uint8_t *sp;   // MUST be first member
    uint8_t stack[STACK_SIZE];
} tcb_t;


/*
 * These must be global because the assembly accesses them
 * by symbol name.
 */
tcb_t tasks[2];

volatile tcb_t *current;


/* =========================================================
 * SAVE_CONTEXT
 *
 * The CALL/interrupt has already pushed the return PC.
 *
 * Additional frame:
 *
 *      r0
 *      SREG
 *      r1
 *      r2
 *      ...
 *      r31
 *
 * Total context:
 *
 *      2 bytes PC
 *      1 byte  r0
 *      1 byte  SREG
 *      31 bytes r1-r31
 *
 *      = 35 bytes
 * ========================================================= */

#define SAVE_CONTEXT()                                      \
    asm volatile (                                          \
        "push r0                \n\t"                       \
        "in   r0, __SREG__      \n\t"                       \
        "cli                    \n\t"                       \
        "push r0                \n\t"                       \
        "push r1                \n\t"                       \
        "clr  r1                \n\t"                       \
        "push r2                \n\t"                       \
        "push r3                \n\t"                       \
        "push r4                \n\t"                       \
        "push r5                \n\t"                       \
        "push r6                \n\t"                       \
        "push r7                \n\t"                       \
        "push r8                \n\t"                       \
        "push r9                \n\t"                       \
        "push r10               \n\t"                       \
        "push r11               \n\t"                       \
        "push r12               \n\t"                       \
        "push r13               \n\t"                       \
        "push r14               \n\t"                       \
        "push r15               \n\t"                       \
        "push r16               \n\t"                       \
        "push r17               \n\t"                       \
        "push r18               \n\t"                       \
        "push r19               \n\t"                       \
        "push r20               \n\t"                       \
        "push r21               \n\t"                       \
        "push r22               \n\t"                       \
        "push r23               \n\t"                       \
        "push r24               \n\t"                       \
        "push r25               \n\t"                       \
        "push r26               \n\t"                       \
        "push r27               \n\t"                       \
        "push r28               \n\t"                       \
        "push r29               \n\t"                       \
        "push r30               \n\t"                       \
        "push r31               \n\t"                       \
                                                            \
        /* X = current */                                   \
        "lds  r26, current     \n\t"                       \
        "lds  r27, current+1   \n\t"                       \
                                                            \
        /* Save SP into current->sp */                      \
        "in   r0, __SP_L__     \n\t"                       \
        "st   X+, r0           \n\t"                       \
        "in   r0, __SP_H__     \n\t"                       \
        "st   X+, r0           \n\t"                       \
    )


/* =========================================================
 * RESTORE_CONTEXT
 *
 * Load current task's SP.
 *
 * Then restore:
 *
 *      r31
 *      r30
 *      ...
 *      r1
 *      SREG
 *      r0
 *
 * It does NOT execute RET or RETI.
 *
 * The caller decides:
 *
 *      start_first() -> RET
 *      Timer ISR     -> RETI
 * ========================================================= */

#define RESTORE_CONTEXT()                                   \
    asm volatile (                                           \
        /* X = current */                                    \
        "lds  r26, current     \n\t"                        \
        "lds  r27, current+1   \n\t"                        \
                                                            \
        /* Load current->sp */                               \
        "ld   r28, X+          \n\t"                        \
        "out  __SP_L__, r28    \n\t"                        \
        "ld   r29, X+          \n\t"                        \
        "out  __SP_H__, r29    \n\t"                        \
                                                            \
        "pop  r31              \n\t"                        \
        "pop  r30              \n\t"                        \
        "pop  r29              \n\t"                        \
        "pop  r28              \n\t"                        \
        "pop  r27              \n\t"                        \
        "pop  r26              \n\t"                        \
        "pop  r25              \n\t"                        \
        "pop  r24              \n\t"                        \
        "pop  r23              \n\t"                        \
        "pop  r22              \n\t"                        \
        "pop  r21              \n\t"                        \
        "pop  r20              \n\t"                        \
        "pop  r19              \n\t"                        \
        "pop  r18              \n\t"                        \
        "pop  r17              \n\t"                        \
        "pop  r16              \n\t"                        \
        "pop  r15              \n\t"                        \
        "pop  r14              \n\t"                        \
        "pop  r13              \n\t"                        \
        "pop  r12              \n\t"                        \
        "pop  r11              \n\t"                        \
        "pop  r10              \n\t"                        \
        "pop  r9               \n\t"                        \
        "pop  r8               \n\t"                        \
        "pop  r7               \n\t"                        \
        "pop  r6               \n\t"                        \
        "pop  r5               \n\t"                        \
        "pop  r4               \n\t"                        \
        "pop  r3               \n\t"                        \
        "pop  r2               \n\t"                        \
        "pop  r1               \n\t"                        \
                                                            \
        /* Restore saved SREG */                             \
        "pop  r0               \n\t"                        \
        "out  __SREG__, r0     \n\t"                        \
                                                            \
        /* Restore original r0 */                           \
        "pop  r0               \n\t"                        \
    )


/* =========================================================
 * SWITCH CURRENT TASK
 * ========================================================= */

__attribute__((noinline, used))
void switch_current_task(void)
{
    if (current == &tasks[0])
        current = &tasks[1];
    else
        current = &tasks[0];
}


/* =========================================================
 * TIMER1 PREEMPTION ISR
 *
 * This is the heart of Stage B.
 *
 * Timer interrupt
 *      ↓
 * SAVE_CONTEXT()
 *      ↓
 * Save current task's SP
 *      ↓
 * switch_current_task()
 *      ↓
 * current now points to other task
 *      ↓
 * RESTORE_CONTEXT()
 *      ↓
 * RETI
 *      ↓
 * other task continues
 * ========================================================= */

ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
    SAVE_CONTEXT();

    asm volatile (
        "call switch_current_task \n\t"
        ::: "memory"
    );

    RESTORE_CONTEXT();

    asm volatile (
        "reti \n\t"
    );
}


/* =========================================================
 * CREATE INITIAL TASK STACK
 * ========================================================= */

static void create_task(tcb_t *t, void (*fn)(void))
{
    memset(t->stack, 0xAA, STACK_SIZE);

    /*
     * Start at the top of the stack.
     *
     * PUSH writes and then decrements SP.
     * Therefore POP later increments before reading.
     */
    uint8_t *sp = &t->stack[STACK_SIZE - 1];


    /*
     * Fake return address.
     *
     * When RET is eventually executed, the CPU will jump
     * to this address, which is the task function.
     */
    uint16_t pc = (uint16_t)(uintptr_t)fn;


    /*
     * Build the fake context frame:
     *
     *      PC low
     *      PC high
     *      r0
     *      SREG
     *      r1
     *      r2
     *      ...
     *      r31
     */

    *sp-- = (uint8_t)(pc & 0xFF);   // PC low
    *sp-- = (uint8_t)(pc >> 8);     // PC high

    *sp-- = 0x00;                   // r0

    /*
     * SREG:
     *
     * bit 7 = I = 1
     *
     * Therefore interrupts will be enabled when
     * this task's context is restored.
     */
    *sp-- = 0x80;

    *sp-- = 0x00;                   // r1

    for (uint8_t r = 2; r <= 31; r++)
        *sp-- = 0x00;


    /*
     * SP now points immediately below r31.
     *
     * RESTORE_CONTEXT() starts with:
     *
     *      pop r31
     */
    t->sp = sp;
}


/* =========================================================
 * START FIRST TASK
 *
 * There is no previous task to restore.
 *
 * We restore the fake context created by create_task()
 * and then RET into task_a().
 * ========================================================= */

__attribute__((naked, noreturn))
void start_first(void)
{
    RESTORE_CONTEXT();

    asm volatile (
        "ret \n\t"
    );
}


/* =========================================================
 * TASK A
 * ========================================================= */

static void task_a(void) { for (;;) { uart_putc('A', NULL); } }


/* =========================================================
 * TASK B
 * ========================================================= */

static void task_b(void)
{
    for (;;) {
        uart_putc('B', NULL);
        _delay_ms(200);
    }
}


/* =========================================================
 * MAIN
 * ========================================================= */

int main(void)
{
    uart_init();

    create_task(&tasks[0], task_a);
    create_task(&tasks[1], task_b);

    /*
     * Start with task A.
     */
    current = &tasks[0];

    /*
     * Enable Timer1 preemption.
     */
    timer1_init();

    /*
     * Enable global interrupts.
     */
    sei();

    /*
     * Restore task A's fake context and jump to task_a().
     */
    start_first();

    while (1) {
    }
}
