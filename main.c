#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
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
 * Semaphore
 * ========================================================= */

typedef struct
{
    volatile uint8_t count;
} os_sem_t;


/* =========================================================
 * Queue
 * ========================================================= */

#define QUEUE_LEN 8

typedef struct
{
    uint8_t buf[QUEUE_LEN];
    volatile uint8_t head, tail;
    os_sem_t items, spaces;
} os_queue_t;


/* =========================================================
 * Globals
 * ========================================================= */

static os_sem_t   uart_lock;
static os_queue_t q;


/* =========================================================
 * Task control block
 * ========================================================= */

#define STACK_SIZE 160
#define MAX_TASKS  5

typedef struct
{
    volatile uint8_t *sp;      /* MUST stay first */
    uint8_t state;
    uint8_t prio;
    uint16_t wake;
    os_sem_t *wait_on;
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
     * Wake delayed tasks and tasks waiting on
     * an available semaphore.
     */
    for (uint8_t i = 0; i < ntasks; i++)
    {
        if (tasks[i].state == T_DELAYED &&
            (int16_t)(ticks - tasks[i].wake) >= 0)
        {
            tasks[i].state = T_READY;
        }
        else if (tasks[i].state == T_WAITING &&
                 tasks[i].wait_on->count > 0)
        {
            tasks[i].state = T_READY;
        }
    }


    /*
     * Find highest priority among READY tasks.
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

    asm volatile (
        "reti \n\t"
    );
}


/* =========================================================
 * Delay
 * ========================================================= */

__attribute__((noinline))
void os_delay(uint16_t n)
{
    cli();

    ((tcb_t *)current)->wake = ticks + n;

    ((tcb_t *)current)->state = T_DELAYED;

    os_yield();
}


/* =========================================================
 * Semaphores
 * ========================================================= */

void os_sem_init(os_sem_t *s, uint8_t initial)
{
    s->count = initial;
}


void os_sem_wait(os_sem_t *s)
{
    for (;;)
    {
        cli();

        if (s->count)
        {
            s->count--;

            sei();

            return;
        }

        ((tcb_t *)current)->state = T_WAITING;

        ((tcb_t *)current)->wait_on = s;

        os_yield();

        /*
         * os_yield() comes back with interrupts enabled.
         * If another task did not give us the semaphore,
         * retry.
         */
    }
}


void os_sem_post(os_sem_t *s)
{
    cli();

    s->count++;

    sei();
}


/* =========================================================
 * Queue
 * ========================================================= */

void os_queue_init(os_queue_t *q)
{
    q->head = q->tail = 0;

    os_sem_init(&q->items, 0);
    os_sem_init(&q->spaces, QUEUE_LEN);
}


void os_queue_put(os_queue_t *q, uint8_t v)
{
    os_sem_wait(&q->spaces);          /* block if full */

    cli();

    q->buf[q->head] = v;
    q->head = (q->head + 1) % QUEUE_LEN;

    sei();

    os_sem_post(&q->items);
}


uint8_t os_queue_get(os_queue_t *q)
{
    os_sem_wait(&q->items);           /* block if empty */

    cli();

    uint8_t v = q->buf[q->tail];
    q->tail = (q->tail + 1) % QUEUE_LEN;

    sei();

    os_sem_post(&q->spaces);

    return v;
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

    t->wait_on = NULL;

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
 * Printing helpers
 * ========================================================= */

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++, NULL);
}


static void uart_print_u8(uint8_t v)
{
    char buf[3];
    uint8_t n = 0;

    do
    {
        buf[n++] = '0' + v % 10;
        v /= 10;
    }
    while (v);

    while (n)
        uart_putc(buf[--n], NULL);
}


/* =========================================================
 * Demo
 * ========================================================= */

static void print_line(char c)
{
    for (uint8_t i = 0; i < 40; i++)
        uart_putc(c, NULL);

    uart_putc('\r', NULL);
    uart_putc('\n', NULL);
}


static void producer_task(void)
{
    uint8_t n = 0;

    for (;;)
    {
        os_queue_put(&q, n++);

        os_delay(250);
    }
}


static void consumer_task(void)
{
    for (;;)
    {
        uint8_t v = os_queue_get(&q);

        os_sem_wait(&uart_lock);

        uart_puts("consumer got ");
        uart_print_u8(v);
        uart_puts("\r\n");

        os_sem_post(&uart_lock);
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

    os_sem_init(&uart_lock, 1);
    os_queue_init(&q);

    create_task(idle_task, 0);
    create_task(producer_task, 1);
    create_task(consumer_task, 1);

    os_schedule();
    timer1_init();

    /*
     * IMPORTANT:
     * Do NOT call sei() here.
     *
     * The first task's fake SREG already has I = 1.
     */
    start_first();
}
