
// ============================================================================
// ELEC4020 / ELEC5200 — Lab 3
// Exercise 2 (Timer0A blinks PF2, SW1/SW2 adjust frequency 0.5..9.5 Hz)
// Exercise 3 (Timer1A captures PF2 via PB4, displays frequency on 7-seg)
//
// One board. Jumper wire:  PF2  →  PB4
//
// 7-seg: A=PC4 B=PC6 C=PE2 D=PD3 E=PD2 F=PC5 G=PC7 DP=PE1
//        DIG1=PA2 DIG2=PA3 DIG3=PA4  (common cathode, HIGH = ON)
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include "inc/tm4c123gh6pm.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/timer.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/systick.h"

// ---------------------------------------------------------------------------
// Function prototypes
// ---------------------------------------------------------------------------
void Timer0AIntHandler(void);   // Exercise 2: blink PF2
void GPIOPortFIntHandler(void); // Exercise 2: SW1/SW2 frequency control
void Timer1AIntHandler(void);   // Exercise 3: capture PF2 period
void SysTickIntHandler(void);   // Exercise 3: 7-seg multiplex

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
volatile uint32_t g_ui32Clock;

// --- Exercise 2 state ---
volatile uint32_t g_ui32Frequency_x10 = 50;   // 5.0 Hz

#define FREQ_MIN_x10   5U
#define FREQ_MAX_x10   95U
#define FREQ_STEP_x10  5U

// --- Exercise 3 state ---
volatile uint32_t g_ui32LastCapture = 0;
volatile bool     g_bFirstCapture  = true;
volatile uint8_t  g_ui8FreqInt   = 5;         // display "5.0" initially
volatile uint8_t  g_ui8FreqTenth = 0;

// ---------------------------------------------------------------------------
// 7-segment helpers (same as your working Lab 2 code)
// ---------------------------------------------------------------------------
#define SEG_PORTC_MASK  (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7)
#define SEG_PORTD_MASK  (GPIO_PIN_2 | GPIO_PIN_3)
#define SEG_PORTE_MASK  (GPIO_PIN_1 | GPIO_PIN_2)
#define DIGIT_MASK      (GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4)

static const uint8_t SEG_TABLE[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static void write_segments(uint8_t p)
{
    GPIOPinWrite(GPIO_PORTC_BASE, SEG_PORTC_MASK,
                   ((p & 0x01) << 4) | ((p & 0x02) << 5) |
                   ((p & 0x20) << 0) | ((p & 0x40) << 1));
    GPIOPinWrite(GPIO_PORTD_BASE, SEG_PORTD_MASK,
                   ((p & 0x08) << 0) | ((p & 0x10) >> 2));
    GPIOPinWrite(GPIO_PORTE_BASE, SEG_PORTE_MASK,
                   ((p & 0x04) << 0) | ((p & 0x80) >> 6));
}
static void digit_select(uint8_t digit)
{
    uint8_t bits = 0;
    switch (digit) {
        case 1:  bits = GPIO_PIN_2; break;
        case 2:  bits = GPIO_PIN_3; break;
        case 3:  bits = GPIO_PIN_4; break;
        default: bits = 0;          break;
    }
    GPIOPinWrite(GPIO_PORTA_BASE, DIGIT_MASK, bits);
}

// ---------------------------------------------------------------------------
// Update Timer0A load value from g_ui32Frequency_x10 (Exercise 2)
// ---------------------------------------------------------------------------
static void UpdateTimerPeriod(void)
{
    uint32_t halfPeriod = (g_ui32Clock * 5U) / g_ui32Frequency_x10;
    if (halfPeriod == 0U) halfPeriod = 1U;
    TimerLoadSet(TIMER0_BASE, TIMER_A, halfPeriod - 1U);
}

// ===========================================================================
// main
// ===========================================================================
int main(void)
{
    // 1. System clock 40 MHz
    SysCtlClockSet(SYSCTL_SYSDIV_40 | SYSCTL_USE_PLL |
                   SYSCTL_XTAL_16MHZ | SYSCTL_OSC_MAIN);
    g_ui32Clock = SysCtlClockGet();

    // 2. Enable peripherals
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER1)) {}

    // -----------------------------------------------------------------------
    // Exercise 2 — GPIO: PF2 output (blue LED), PF4/PF0 inputs (buttons)
    // -----------------------------------------------------------------------
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_2);
    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 0);

    GPIOPinTypeGPIOInput(GPIO_PORTF_BASE, GPIO_PIN_4);
    GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_4,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    // PF0 unlock (SW2)
    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR)  |= 0x01;
    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = 0;
    GPIOPinTypeGPIOInput(GPIO_PORTF_BASE, GPIO_PIN_0);
    GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_0,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    // -----------------------------------------------------------------------
    // Exercise 3 — display outputs + PB4 as T1CCP0
    // -----------------------------------------------------------------------
    GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, DIGIT_MASK);
    GPIOPinTypeGPIOOutput(GPIO_PORTC_BASE, SEG_PORTC_MASK);
    GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE, SEG_PORTD_MASK);
    GPIOPinTypeGPIOOutput(GPIO_PORTE_BASE, SEG_PORTE_MASK);
    GPIOPinWrite(GPIO_PORTA_BASE, DIGIT_MASK, 0);
    GPIOPinWrite(GPIO_PORTC_BASE, SEG_PORTC_MASK, 0);
    GPIOPinWrite(GPIO_PORTD_BASE, SEG_PORTD_MASK, 0);
    GPIOPinWrite(GPIO_PORTE_BASE, SEG_PORTE_MASK, 0);

    GPIOPinConfigure(GPIO_PB4_T1CCP0);
    GPIOPinTypeTimer(GPIO_PORTB_BASE, GPIO_PIN_4);

    // -----------------------------------------------------------------------
    // Timer0A — Exercise 2 periodic blink
    // -----------------------------------------------------------------------
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    UpdateTimerPeriod();
    TimerIntRegister(TIMER0_BASE, TIMER_A, Timer0AIntHandler);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);

    

    // -----------------------------------------------------------------------
    // GPIOF interrupts — SW1 falling, SW2 rising
    // -----------------------------------------------------------------------
    GPIOIntRegister(GPIO_PORTF_BASE, GPIOPortFIntHandler);
    GPIOIntTypeSet(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_FALLING_EDGE);
    GPIOIntTypeSet(GPIO_PORTF_BASE, GPIO_PIN_0, GPIO_RISING_EDGE);
    GPIOIntEnable(GPIO_PORTF_BASE, GPIO_INT_PIN_4 | GPIO_INT_PIN_0);
    IntEnable(INT_GPIOF);

    // -----------------------------------------------------------------------
    // Timer1A — Exercise 3 32-bit capture, rising edge
    // -----------------------------------------------------------------------
    // TimerConfigure(TIMER1_BASE, TIMER_CFG_A_CAP_TIME_UP);   // 32-bit, NOT split
    // TimerControlEvent(TIMER1_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);
    // TimerLoadSet(TIMER1_BASE, TIMER_A, 0xFFFFFFFF);
    // TimerEnable(TIMER1_BASE, TIMER_A);
    // TimerIntRegister(TIMER1_BASE, TIMER_A, Timer1AIntHandler);
    // TimerIntEnable(TIMER1_BASE, TIMER_CAPA_EVENT);
    // IntEnable(INT_TIMER1A);

    // Enable peripherals for GPIO and Timer1
SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);

// Configure PB4 as a timer input (T1CCP0)
GPIOPinConfigure(GPIO_PB4_T1CCP0);
GPIOPinTypeTimer(GPIO_PORTB_BASE, GPIO_PIN_4);

// Configure Timer1 as a 32-bit edge-time capture timer (NOT split)
TimerConfigure(TIMER1_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME_UP);
TimerControlEvent(TIMER1_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);
TimerLoadSet(TIMER1_BASE, TIMER_A, 0xFFFFFFFF);
TimerEnable(TIMER1_BASE, TIMER_A);


TimerIntRegister(TIMER1_BASE, TIMER_A, Timer1AIntHandler);
TimerIntEnable(TIMER1_BASE, TIMER_CAPA_EVENT);
IntEnable(INT_TIMER1A);


    //    // Enable peripherals for GPIO and Timer0
    // SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    // SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    // // Configure PB6 as a timer input (T0CCP0)
    // GPIOPinConfigure(GPIO_PB6_T0CCP0);
    // GPIOPinTypeTimer(GPIO_PORTB_BASE, GPIO_PIN_6);
    
    // // Enable peripherals for GPIO and Timer0
    // SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    // SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    // // Configure PB6 as a timer input (T0CCP0)
    // GPIOPinConfigure(GPIO_PB6_T0CCP0);
    // GPIOPinTypeTimer(GPIO_PORTB_BASE, GPIO_PIN_6);
    // // Configure Timer0 as a 24-bit periodic timer in edge-time mode
    // TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME_UP);
    // TimerControlEvent(TIMER0_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);


    // -----------------------------------------------------------------------
    // SysTick — 10 ms display multiplex
    // -----------------------------------------------------------------------
    SysTickPeriodSet(g_ui32Clock / 100 - 1);
    SysTickIntEnable();
    SysTickEnable();
    SysTickIntRegister(SysTickIntHandler);

    IntMasterEnable();
    TimerEnable(TIMER0_BASE, TIMER_A);

    while (1) { }
}

// ===========================================================================
// Exercise 2 ISRs
// ===========================================================================
void Timer0AIntHandler(void)
{
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    if (GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_2))
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 0);
    else
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 4);
}

void GPIOPortFIntHandler(void)
{
    uint32_t status = GPIOIntStatus(GPIO_PORTF_BASE, true);

    if (status & GPIO_INT_PIN_4) {          // SW1 falling → +0.5 Hz
        GPIOIntClear(GPIO_PORTF_BASE, GPIO_INT_PIN_4);
        if (g_ui32Frequency_x10 < FREQ_MAX_x10) {
            g_ui32Frequency_x10 += FREQ_STEP_x10;
            UpdateTimerPeriod();
        }
    }
    if (status & GPIO_INT_PIN_0) {          // SW2 rising → −0.5 Hz
        GPIOIntClear(GPIO_PORTF_BASE, GPIO_INT_PIN_0);
        if (g_ui32Frequency_x10 > FREQ_MIN_x10) {
            g_ui32Frequency_x10 -= FREQ_STEP_x10;
            UpdateTimerPeriod();
        }
    }
}

// ===========================================================================
// Exercise 3 ISRs
// ===========================================================================

// void Timer0IntHandler(void)
// {
//     TimerIntClear(TIMER0_BASE, TIMER_CAPA_EVENT);
    
//     Point1 = Point2;
//     Point2 = TimerValueGet(TIMER0_BASE, TIMER_A);

//     uint32_t diff;
//     if (Point2 >= Point1)
//         diff = Point2 - Point1;
//     else
//         diff = 0x10000 + Point2 - Point1;   // 16-bit wrap, not 2^24

//     if (diff != 0)
//         Frequency = g_ui32Clock / diff;
// }

volatile uint32_t Point1, Point2;
volatile float Frequency;

void Timer1AIntHandler(void)
{
    TimerIntClear(TIMER1_BASE, TIMER_CAPA_EVENT);
    uint32_t current = TimerValueGet(TIMER1_BASE, TIMER_A);

    // if (g_bFirstCapture) {
    //     g_bFirstCapture = false;
    //     g_ui32LastCapture = current;
    //     return;
    // }

    // uint32_t period = current - g_ui32LastCapture;
    // g_ui32LastCapture = current;
    // if (period == 0) return;

    Point1 = Point2;
    Point2 = TimerValueGet(TIMER1_BASE, TIMER_A);

    uint32_t diff;
    // if (Point2 >= Point1)
    //     diff = Point2 - Point1;
    // else
    //     diff = 0x10000 + Point2 - Point1;   // 16-bit wrap, not 2^24

    // if (diff != 0)
    //     Frequency = g_ui32Clock / diff;

    if (Point2>Point1)
    {
    Frequency = (float)g_ui32Clock/(Point2 - Point1);
    }
    if (Point2<Point1)
    {
    Frequency = (float)g_ui32Clock/(16777216 + Point2 - Point1);
    }

    uint32_t f10 = (Frequency * 10U);
    if (f10 < 5U)  f10 = 5U;
    if (f10 > 95U) f10 = 95U;
    g_ui8FreqInt   = (uint8_t)(f10 / 10U);
    g_ui8FreqTenth = (uint8_t)(f10 % 10U);
}


// void Timer1AIntHandler(void)
// {
//     TimerIntClear(TIMER1_BASE, TIMER_CAPA_EVENT);
//     uint32_t current = TimerValueGet(TIMER1_BASE, TIMER_A);

//     if (g_bFirstCapture) {
//         g_bFirstCapture = false;
//         g_ui32LastCapture = current;
//         return;
//     }

//     uint32_t period = current - g_ui32LastCapture;
//     g_ui32LastCapture = current;
//     if (period == 0) return;

//     uint32_t f10 = (g_ui32Clock * 10U) / period;
//     if (f10 < 5U)  f10 = 5U;
//     if (f10 > 95U) f10 = 95U;
//     g_ui8FreqInt   = (uint8_t)(f10 / 10U);
//     g_ui8FreqTenth = (uint8_t)(f10 % 10U);
// }

void SysTickIntHandler(void)
{
    static uint8_t d = 0;
    digit_select(0);                            // all off to avoid ghosting

    if (d == 0) {
        write_segments(SEG_TABLE[g_ui8FreqInt] | 0x80);   // "X"
        digit_select(1);
    } else if (d == 1) {
        write_segments(SEG_TABLE[g_ui8FreqTenth]);  // ".Y"
        digit_select(2);
    } else {
        write_segments(SEG_TABLE[0]);
        digit_select(3);
    }
    d = (d + 1) % 3;
}














