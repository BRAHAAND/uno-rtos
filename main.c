#define F_CPU 16000000UL

#include <avr/io.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <avr/interrupt.h>


/* =========================================================
 * Forward declarations
 * ========================================================= */

typedef struct tcb tcb_t;
typedef struct os_mutex os_mutex_t;


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


static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++, NULL);
}


static void __attribute__((unused)) uart_print_u8(uint8_t v)
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
 * Priority-inheritance mutex
 * ========================================================= */

struct os_mutex
{
    volatile tcb_t *owner;
};


/* =========================================================
 * Queue
 * ========================================================= */

#define QUEUE_LEN 8

typedef struct
{
    uint8_t buf[QUEUE_LEN];
    volatile uint8_t head;
    volatile uint8_t tail;

    os_sem_t items;
    os_sem_t spaces;
} os_queue_t;


/* =========================================================
 * Globals
 * ========================================================= */


/* =========================================================
 * Task control block
 * ========================================================= */

#define STACK_SIZE 160
#define MAX_TASKS  5

struct tcb
{
    volatile uint8_t *sp;      /* MUST stay first */

    uint8_t state;

    /*
     * prio:
     *     Current/effective priority.
     *
     * base_prio:
     *     Original priority assigned when task
     *     was created.
     */
    uint8_t prio;
    uint8_t base_prio;

    uint16_t wake;

    os_sem_t *wait_on;

    /*
     * Non-NULL when this task is waiting for
     * a priority-inheritance mutex.
     */
    os_mutex_t *wait_mutex;

    uint8_t stack[STACK_SIZE];
};


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
     * Wake delayed tasks.
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
     * Wake tasks waiting for semaphores.
     */
    for (uint8_t i = 0; i < ntasks; i++)
    {
        if (tasks[i].state == T_WAITING &&
            tasks[i].wait_on != NULL &&
            tasks[i].wait_mutex == NULL &&
            tasks[i].wait_on->count > 0)
        {
            tasks[i].state = T_READY;
            tasks[i].wait_on = NULL;
        }
    }


    /*
     * Wake tasks waiting for a mutex when
     * the mutex has become free.
     */
    for (uint8_t i = 0; i < ntasks; i++)
    {
        if (tasks[i].state == T_WAITING &&
            tasks[i].wait_mutex != NULL &&
            tasks[i].wait_mutex->owner == NULL)
        {
            tasks[i].state = T_READY;
        }
    }


    /*
     * Find highest effective priority among
     * READY tasks.
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
        ((tcb_t *)current)->wait_mutex = NULL;

        os_yield();
    }
}


void os_sem_post(os_sem_t *s)
{
    cli();

    s->count++;

    sei();
}


/* =========================================================
 * Priority-inheritance mutex
 * ========================================================= */

void os_mutex_init(os_mutex_t *m)
{
    m->owner = NULL;
}


/*
 * Lock the mutex.
 *
 * If the mutex is free:
 *
 *     current becomes owner.
 *
 * If another task owns it:
 *
 *     current blocks.
 *
 *     If current has higher priority than the owner,
 *     temporarily raise the owner's effective priority.
 */
void os_mutex_lock(os_mutex_t *m)
{
    for (;;)
    {
        cli();

        /*
         * Mutex is free.
         */
        if (m->owner == NULL)
        {
            m->owner = (tcb_t *)current;

            ((tcb_t *)current)->wait_mutex = NULL;

            sei();

            return;
        }


        /*
         * Mutex is already owned.
         */
        if (m->owner != current)
        {
            tcb_t *owner = (tcb_t *)m->owner;
            tcb_t *me = (tcb_t *)current;


            /*
             * Priority inheritance:
             *
             * If the waiting task has a higher priority
             * than the owner, boost the owner.
             */
            if (me->prio > owner->prio)
            {
                owner->prio = me->prio;
            }


            /*
             * Block the current task.
             */
            me->wait_mutex = m;
            me->state = T_WAITING;

            os_yield();

            /*
             * When this task is scheduled again,
             * retry the lock acquisition.
             */
        }
        else
        {
            /*
             * Recursive locking is not supported.
             */
            sei();
            return;
        }
    }
}


/*
 * Unlock the mutex.
 *
 * The owner releases the mutex and returns to its
 * original priority.
 */
void os_mutex_unlock(os_mutex_t *m)
{
    cli();

    tcb_t *me = (tcb_t *)current;

    if (m->owner != me)
    {
        sei();
        return;
    }


    /*
     * Release the mutex.
     */
    m->owner = NULL;


    /*
     * Remove priority inheritance.
     */
    me->prio = me->base_prio;


    /*
     * Immediately reschedule.
     *
     * A higher-priority waiting task may now
     * acquire the mutex.
     */
    os_yield();
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
    os_sem_wait(&q->spaces);

    cli();

    q->buf[q->head] = v;
    q->head = (q->head + 1) % QUEUE_LEN;

    sei();

    os_sem_post(&q->items);
}


uint8_t os_queue_get(os_queue_t *q)
{
    os_sem_wait(&q->items);

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
     * Build fake stack frame.
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

    /*
     * Save both priorities.
     */
    t->prio = prio;
    t->base_prio = prio;

    t->state = T_READY;

    t->wake = 0;

    t->wait_on = NULL;

    t->wait_mutex = NULL;

    return ntasks++;
}


/* =========================================================
 * Stack usage
 * ========================================================= */

uint8_t os_stack_free(uint8_t i)
{
    uint8_t n = 0;

    while (n < STACK_SIZE && tasks[i].stack[n] == 0xAA)
        n++;

    return n;
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
 * Priority-inheritance demonstration
 * ========================================================= */

static os_mutex_t test_mutex;


/* ---------------------------------------------------------
 * LOW priority task
 * --------------------------------------------------------- */

static void low_task(void)
{
    uart_puts("LOW: starting\r\n");

    /*
     * Give LOW time to start before HIGH tries
     * to acquire the mutex.
     */
    os_delay(20);

    uart_puts("LOW: trying to acquire mutex\r\n");

    os_mutex_lock(&test_mutex);

    uart_puts("LOW: acquired mutex\r\n");

    /*
     * Hold the mutex while doing work.
     *
     * HIGH will try to acquire it.
     */
    for (volatile uint32_t i = 0; i < 500000UL; i++)
    {
        /* simulate work */
    }

    uart_puts("LOW: releasing mutex\r\n");

    os_mutex_unlock(&test_mutex);

    uart_puts("LOW: priority restored\r\n");

    for (;;)
        os_delay(1000);
}


/* ---------------------------------------------------------
 * MEDIUM priority task
 * --------------------------------------------------------- */

static void medium_task(void)
{
    /*
     * Wait until LOW has acquired the mutex
     * and HIGH has had a chance to block.
     */
    os_delay(50);

    uart_puts("MEDIUM: running\r\n");

    for (;;)
    {
        volatile uint32_t x = 0;

        for (uint32_t i = 0; i < 50000UL; i++)
            x++;

        os_delay(10);
    }
}


/* ---------------------------------------------------------
 * HIGH priority task
 * --------------------------------------------------------- */

static void high_task(void)
{
    /*
     * LOW should already own the mutex.
     */
    os_delay(30);

    uart_puts("HIGH: trying to acquire mutex\r\n");

    /*
     * HIGH blocks here.
     *
     * os_mutex_lock() raises LOW's effective
     * priority from 1 to 3.
     */
    os_mutex_lock(&test_mutex);

    uart_puts("HIGH: acquired mutex\r\n");

    os_mutex_unlock(&test_mutex);

    for (;;)
        os_delay(1000);
}


/* =========================================================
 * Idle task
 * ========================================================= */

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

    /*
     * Initialize the priority-inheritance mutex.
     */
    os_mutex_init(&test_mutex);


    /*
     * Priorities:
     *
     * IDLE   = 0
     * LOW    = 1
     * MEDIUM = 2
     * HIGH   = 3
     */
    create_task(idle_task,   0);
    create_task(low_task,    1);
    create_task(medium_task, 2);
    create_task(high_task,   3);


    os_schedule();

    timer1_init();


    /*
     * Do NOT call sei() here.
     *
     * The fake initial stack frame already contains
     * SREG = 0x80, which enables interrupts.
     */
    start_first();
}
