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
    UBRR0 = 16;                         // 115200 baud @ 16 MHz, U2X
    UCSR0B = _BV(TXEN0);
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
 * Timer 1
 * ========================================================= */

static void timer1_init(void)
{
    TCCR1A = 0;

    TCCR1B = _BV(WGM12) |
             _BV(CS11)  |
             _BV(CS10);       // CTC, /64

    OCR1A = 249;               // 1 kHz tick @ 16 MHz / 64

    TIMSK1 = _BV(OCIE1A);
}


/* =========================================================
 * Task states
 * ========================================================= */

enum
{
    T_FREE = 0,
    T_READY,
    T_DELAYED,
    T_WAITING
};


/* =========================================================
 * Task control block
 * ========================================================= */

#define STACK_SIZE 160
#define MAX_TASKS  3

typedef struct
{
    volatile uint8_t *sp;      /* MUST stay first */
    uint8_t state;
    uint8_t prio;
    uint16_t wake;
    uint8_t stack[STACK_SIZE];
} tcb_t;


tcb_t tasks[MAX_TASKS];

static uint8_t ntasks = 0;

volatile tcb_t *current = 0;


/* =========================================================
 * Context save
 * ========================================================= */

#define SAVE_CONTEXT() asm volatile ( \
    "push r0                    \n\t" \
    "in r0, __SREG__            \n\t" \
    "cli                        \n\t" \
    "push r0                    \n\t" \
    "push r1                    \n\t" \
    "clr r1                     \n\t" \
    "push r2                    \n\t" \
    "push r3                    \n\t" \
    "push r4                    \n\t" \
    "push r5                    \n\t" \
    "push r6                    \n\t" \
    "push r7                    \n\t" \
    "push r8                    \n\t" \
    "push r9                    \n\t" \
    "push r10                   \n\t" \
    "push r11                   \n\t" \
    "push r12                   \n\t" \
    "push r13                   \n\t" \
    "push r14                   \n\t" \
    "push r15                   \n\t" \
    "push r16                   \n\t" \
    "push r17                   \n\t" \
    "push r18                   \n\t" \
    "push r19                   \n\t" \
    "push r20                   \n\t" \
    "push r21                   \n\t" \
    "push r22                   \n\t" \
    "push r23                   \n\t" \
    "push r24                   \n\t" \
    "push r25                   \n\t" \
    "push r26                   \n\t" \
    "push r27                   \n\t" \
    "push r28                   \n\t" \
    "push r29                   \n\t" \
    "push r30                   \n\t" \
    "push r31                   \n\t" \
    "lds r26, current           \n\t" \
    "lds r27, current+1         \n\t" \
    "in r0, __SP_L__            \n\t" \
    "st X+, r0                  \n\t" \
    "in r0, __SP_H__            \n\t" \
    "st X+, r0                  \n\t" \
)


/* =========================================================
 * Context restore
 * ========================================================= */

#define RESTORE_CONTEXT() asm volatile ( \
    "lds r26, current           \n\t" \
    "lds r27, current+1         \n\t" \
    "ld r28, X+                 \n\t" \
    "out __SP_L__, r28          \n\t" \
    "ld r29, X+                 \n\t" \
    "out __SP_H__, r29          \n\t" \
    "pop r31                    \n\t" \
    "pop r30                    \n\t" \
    "pop r29                    \n\t" \
    "pop r28                    \n\t" \
    "pop r27                    \n\t" \
    "pop r26                    \n\t" \
    "pop r25                    \n\t" \
    "pop r24                    \n\t" \
    "pop r23                    \n\t" \
    "pop r22                    \n\t" \
    "pop r21                    \n\t" \
    "pop r20                    \n\t" \
    "pop r19                    \n\t" \
    "pop r18                    \n\t" \
    "pop r17                    \n\t" \
    "pop r16                    \n\t" \
    "pop r15                    \n\t" \
    "pop r14                    \n\t" \
    "pop r13                    \n\t" \
    "pop r12                    \n\t" \
    "pop r11                    \n\t" \
    "pop r10                    \n\t" \
    "pop r9                     \n\t" \
    "pop r8                     \n\t" \
    "pop r7                     \n\t" \
    "pop r6                     \n\t" \
    "pop r5                     \n\t" \
    "pop r4                     \n\t" \
    "pop r3                     \n\t" \
    "pop r2                     \n\t" \
    "pop r1                     \n\t" \
    "pop r0                     \n\t" \
    "out __SREG__, r0           \n\t" \
    "pop r0                     \n\t" \
)


/* =========================================================
 * Tick counter
 * ========================================================= */

static volatile uint16_t ticks;


/* =========================================================
 * OS tick
 * ========================================================= */

__attribute__((noinline, used))
void os_tick(void)
{
    ticks++;
}


/* =========================================================
 * Scheduler
 * ========================================================= */

__attribute__((noinline, used))
void os_schedule(void)
{
    uint8_t current_index = 0;

    if (current != 0)
        current_index = (uint8_t)(current - tasks);


    /*
     * Wake up tasks whose delay has expired.
     *
     * The signed difference handles 16-bit tick wraparound.
     */
    for (uint8_t i = 0; i < ntasks; i++)
    {
        if (tasks[i].state == T_DELAYED &&
            (int16_t)(ticks - tasks[i].wake) >= 0)
        {
            tasks[i].state = T_READY;
        }
    }


    /*
     * Step 1:
     * Find the highest priority among READY tasks.
     */
    uint8_t highest_prio = 0;

    for (uint8_t i = 0; i < ntasks; i++)
    {
        if (tasks[i].state == T_READY &&
            tasks[i].prio > highest_prio)
        {
            highest_prio = tasks[i].prio;
        }
    }


    /*
     * Step 2:
     * Among highest-priority READY tasks,
     * choose the next task after current.
     */
    uint8_t start = (current_index + 1) % ntasks;

    for (uint8_t n = 0; n < ntasks; n++)
    {
        uint8_t i = (start + n) % ntasks;

        if (tasks[i].state == T_READY &&
            tasks[i].prio == highest_prio)
        {
            current = &tasks[i];
            return;
        }
    }
}


/* =========================================================
 * Yield
 * ========================================================= */

__attribute__((naked, noinline))
void os_yield(void)
{
    SAVE_CONTEXT();

    asm volatile (
        "call os_schedule \n\t"
        ::: "memory"
    );

    RESTORE_CONTEXT();

    /*
     * IMPORTANT:
     * This must be RETI, not RET.
     *
     * os_delay() disables interrupts before calling os_yield().
     * The saved SREG therefore has I = 0.
     *
     * RETI restores execution and enables interrupts.
     */
    asm volatile (
        "reti \n\t"
    );
}


/* =========================================================
 * Delay
 * ========================================================= */

void os_delay(uint16_t n)
{
    cli();

    ((tcb_t *)current)->wake = ticks + n;

    ((tcb_t *)current)->state = T_DELAYED;

    os_yield();
}


/* =========================================================
 * Timer ISR
 * ========================================================= */

ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
    SAVE_CONTEXT();

    asm volatile (
        "call os_tick     \n\t"
        "call os_schedule \n\t"
        ::: "memory"
    );

    RESTORE_CONTEXT();

    asm volatile (
        "reti"
    );
}


/* =========================================================
 * Create task
 * ========================================================= */

static int8_t create_task(void (*fn)(void), uint8_t prio)
{
    if (ntasks >= MAX_TASKS)
        return -1;

    tcb_t *t = &tasks[ntasks];

    memset(t, 0, sizeof(tcb_t));

    memset(t->stack, 0xAA, STACK_SIZE);


    /*
     * Build the fake stack frame.
     *
     * The task will eventually restore this frame and RET
     * into fn().
     */
    uint8_t *sp = &t->stack[STACK_SIZE - 1];

    uint16_t pc = (uint16_t)(uintptr_t)fn;


    /* PC */
    *sp-- = (uint8_t)(pc & 0xFF);
    *sp-- = (uint8_t)(pc >> 8);


    /* r0 */
    *sp-- = 0x00;


    /* SREG: I = 1 */
    *sp-- = 0x80;


    /* r1 */
    *sp-- = 0x00;


    /* r2 ... r31 */
    for (uint8_t r = 2; r <= 31; r++)
        *sp-- = 0x00;


    t->sp = sp;

    t->prio = prio;

    t->state = T_READY;

    t->wake = 0;

    return ntasks++;
}


/* =========================================================
 * Start first task
 * ========================================================= */

__attribute__((naked, noreturn))
void start_first(void)
{
    RESTORE_CONTEXT();

    asm volatile (
        "ret"
    );
}


/* =========================================================
 * Tasks
 * ========================================================= */

static void task_a(void)
{
    for (;;)
    {
        uart_putc('A', NULL);

        os_delay(200);
    }
}

static void task_b(void)
{
    for (;;)
    {
        uart_putc('B', NULL);

        os_delay(200);
    }
}


static void idle_task(void)
{
    for (;;)
    {
    }
}


/* =========================================================
 * Main
 * ========================================================= */

int main(void)
{
    uart_init();

    const char *banner = "7\r\n";
    while (*banner) uart_putc(*banner++, NULL);
    /*
     * Priority:
     *
     * idle = 0
     * A    = 2
     * B    = 1
     */
    create_task(idle_task, 0);

    create_task(task_a, 2);

    create_task(task_b, 1);


    /*
     * Select the first task.
     */
    os_schedule();


    timer1_init();

    sei();


    /*
     * Never returns.
     */
    start_first();


    while (1)
    {
    }
}
