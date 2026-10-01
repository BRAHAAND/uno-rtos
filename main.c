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
    while (!(UCSR0A & _BV(UDRE0))) { }
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
        PINB = _BV(PB5);                  /* toggle LED */
        printf("hello %u\n", n++);
        _delay_ms(500);
    }
}
