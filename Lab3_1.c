#include <stdbool.h>
#include <stdint.h>

#include "inc/hw_memmap.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/systick.h"

/* ------------------------------------------------------------------------
 * 3-digit common-cathode 7-segment display showing 0.00 - 9.99
 *
 * Segment wiring:
 *   A=PC4  B=PC6  C=PE2  D=PD3  E=PD2  F=PC5  G=PC7  DP=PE1
 * Digit select (active high):
 *   Digit 0=PA2  Digit 1=PA3  Digit 2=PA4
 * ------------------------------------------------------------------------ */

/* ---- Configuration ---- */
#define NUM_DIGITS          3
#define TICKS_PER_SECOND    1000    /* SysTick fires every 1 ms         */
#define MS_PER_HUNDREDTH    10      /* 10 ms = 0.01 s                   */
#define COUNT_MAX           1000    /* wraps back to 0 after 9.99       */

#define DIGIT_SELECT_PINS   (GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4)

/* ---- Bit positions in a segment pattern (bit0 = A ... bit6 = G) ---- */
#define SEG_A   0x01
#define SEG_B   0x02
#define SEG_C   0x04
#define SEG_D   0x08
#define SEG_E   0x10
#define SEG_F   0x20
#define SEG_G   0x40

/* Segment patterns for digits 0-9 */
static const uint8_t digitPatterns[10] =
{
    0x3F,   /* 0 */
    0x06,   /* 1 */
    0x5B,   /* 2 */
    0x4F,   /* 3 */
    0x66,   /* 4 */
    0x6D,   /* 5 */
    0x7D,   /* 6 */
    0x07,   /* 7 */
    0x7F,   /* 8 */
    0x6F    /* 9 */
};

/* Digit-select pin for each display position, leftmost first */
static const uint8_t digitSelectPin[NUM_DIGITS] =
{
    GPIO_PIN_2,
    GPIO_PIN_3,
    GPIO_PIN_4
};

/* ---- Shared state (modified in the interrupt handler) ---- */
static volatile uint32_t countHundredths = 0;  /* 0..999 -> 0.00..9.99 */
static volatile uint32_t msCounter       = 0;  /* milliseconds toward the next 0.01 s */
static volatile uint8_t  currentDigit    = 0;  /* which digit is being driven (0..2) */


/* Drive the segment pins for one digit pattern */
static void DriveSegments(uint8_t pattern, bool showDecimalPoint)
{
    /* Port C: A, B, F, G */
    uint8_t portC = 0;
    if (pattern & SEG_A) portC |= GPIO_PIN_4;
    if (pattern & SEG_B) portC |= GPIO_PIN_6;
    if (pattern & SEG_F) portC |= GPIO_PIN_5;
    if (pattern & SEG_G) portC |= GPIO_PIN_7;
    GPIOPinWrite(GPIO_PORTC_BASE,
                 GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
                 portC);

    /* Port E: C, DP */
    uint8_t portE = 0;
    if (pattern & SEG_C) portE |= GPIO_PIN_2;
    if (showDecimalPoint) portE |= GPIO_PIN_1;
    GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_1 | GPIO_PIN_2, portE);

    /* Port D: D, E */
    uint8_t portD = 0;
    if (pattern & SEG_D) portD |= GPIO_PIN_3;
    if (pattern & SEG_E) portD |= GPIO_PIN_2;
    GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3, portD);
}


/* SysTick interrupt: fires every 1 ms */
void SysTickIntHandler(void)
{
    /* Advance the count every 10 ms */
    msCounter++;
    if (msCounter >= MS_PER_HUNDREDTH)
    {
        msCounter = 0;
        countHundredths = (countHundredths + 1) % COUNT_MAX;
    }

    /* Split the count into digits: X.YZ */
    uint8_t digitValues[NUM_DIGITS];
    digitValues[0] = (countHundredths / 100) % 10;   /* units      (X.. ) */
    digitValues[1] = (countHundredths / 10)  % 10;   /* tenths     (.X. ) */
    digitValues[2] =  countHundredths        % 10;   /* hundredths (..X) */

    /* Blank all digits first to prevent ghosting */
    GPIOPinWrite(GPIO_PORTA_BASE, DIGIT_SELECT_PINS, 0x00);

    /* Show the current digit; only the first one gets the decimal point */
    bool showDecimalPoint = (currentDigit == 0);
    DriveSegments(digitPatterns[digitValues[currentDigit]], showDecimalPoint);
    GPIOPinWrite(GPIO_PORTA_BASE, DIGIT_SELECT_PINS, digitSelectPin[currentDigit]);

    /* Move to the next digit for the next interrupt */
    currentDigit = (currentDigit + 1) % NUM_DIGITS;
}


static void InitGpio(void)
{
    /* Enable clocks for GPIO ports A, C, D, E */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);

    /* Digit select pins */
    GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, DIGIT_SELECT_PINS);

    /* Segment pins */
    GPIOPinTypeGPIOOutput(GPIO_PORTC_BASE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
    GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3);
    GPIOPinTypeGPIOOutput(GPIO_PORTE_BASE, GPIO_PIN_1 | GPIO_PIN_2);
}


static void InitSysTick(void)
{
    SysTickPeriodSet(SysCtlClockGet() / TICKS_PER_SECOND - 1);
    SysTickIntEnable();
    SysTickEnable();
}


int main(void)
{
    InitGpio();
    InitSysTick();

    while (1)
    {
        /* Everything happens in the SysTick interrupt */
    }
}
