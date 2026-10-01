#include <avr/io.h>
#include <stdio.h>
#include <avr/interrupt.h>

static volatile uint16_t ticks;

ISR(TIMER1_COMPA_vect)
{
    ticks++;
}

static void timer1_init(void)
{
    TCCR1A = 0;

    /* CTC mode, clock = 16 MHz / 64 = 250 kHz */
    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);

    /* 250 kHz / (249 + 1) = 1 kHz */
    OCR1A = 249;

    /* Enable Timer1 Compare Match A interrupt */
    TIMSK1 = _BV(OCIE1A);
}

static void uart_init(void)
{
    UCSR0A = _BV(U2X0);

    /* 115200 baud @ 16 MHz with U2X */
    UBRR0 = 16;

    UCSR0B = _BV(TXEN0);

    /* 8 data bits, no parity, 1 stop bit */
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

static int uart_putc(char c, FILE *f)
{
    (void)f;

    if (c == '\n')
        uart_putc('\r', f);

    while (!(UCSR0A & _BV(UDRE0))) {
    }

    UDR0 = c;
    return 0;
}

static FILE uart_out =
    FDEV_SETUP_STREAM(uart_putc, NULL, _FDEV_SETUP_WRITE);

int main(void)
{
    uint16_t last = 0;

    /* PB5 as output */
    DDRB |= _BV(PB5);

    uart_init();
    timer1_init();

    stdout = &uart_out;

    /* Globally enable interrupts */
    sei();

    for (;;) {
        cli();
        uint16_t now = ticks;      /* 16-bit read is two instructions on AVR */
        sei();

        if ((uint16_t)(now - last) >= 1000) {
            last += 1000;
            PINB = _BV(PB5);       /* Toggle LED once per second */
            printf("tick %u\n", now);
        }
    }
}

