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
