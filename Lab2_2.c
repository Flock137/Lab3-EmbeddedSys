// ============================================================================
// Lab 3, Exercise 2
// Blink BLUE LED (PF2) using Timer0A interrupt.
// Frequency controlled by SW1 (PF4, falling edge) and SW2 (PF0, rising edge).
// Range: 0.5 Hz .. 9.5 Hz, step 0.5 Hz.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include "inc/tm4c123gh6pm.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "driverlib/gpio.h"
#include "driverlib/timer.h"

// ---------------------------------------------------------------------------
// Function prototypes
// ---------------------------------------------------------------------------
void Timer0AIntHandler(void);
void GPIOPortFIntHandler(void);

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
volatile uint32_t g_ui32Clock;          // System clock frequency (Hz)
volatile uint32_t g_ui32HalfPeriod;     // Half-period in timer ticks
volatile uint32_t g_ui32Frequency_x10;  // Frequency in 0.1 Hz units (50 = 5.0 Hz)

#define FREQ_MIN_x10   5U      //  0.5 Hz
#define FREQ_MAX_x10   95U     //  9.5 Hz
#define FREQ_STEP_x10  5U      //  0.5 Hz
#define FREQ_START_x10 50U     //  5.0 Hz

// ---------------------------------------------------------------------------
// Helper: convert frequency (in 0.1 Hz units) to Timer0A load value.
// Timer0A runs at the system clock. The LED toggles every half period,
// and the timer fires at every reload, so:
//     full period = 1/f  seconds
//     half period = 1/(2f) seconds
//     load value  = clock/(2f) - 1
// ---------------------------------------------------------------------------
static void UpdateTimerPeriod(void)
{
    // g_ui32Frequency_x10 is f * 10, so f = g_ui32Frequency_x10 / 10.
    // half period ticks = clock / (2 * f) = clock * 10 / (2 * g_ui32Frequency_x10)
    //                   = clock * 5 / g_ui32Frequency_x10
    uint32_t halfPeriod = (g_ui32Clock * 5U) / g_ui32Frequency_x10;
    if (halfPeriod == 0U) halfPeriod = 1U;

    g_ui32HalfPeriod = halfPeriod;
    TimerLoadSet(TIMER0_BASE, TIMER_A, halfPeriod - 1U);
}

// ===========================================================================
// main
// ===========================================================================
int main(void)
{
    // -----------------------------------------------------------------------
    // 1. System clock: 40 MHz  (400 MHz PLL / 2 / 5 = 40 MHz)
    //    (SYSDIV_5 gives 400/2/5 = 40 MHz on TM4C123)
    // -----------------------------------------------------------------------
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_XTAL_16MHZ | SYSCTL_OSC_MAIN);
    g_ui32Clock = SysCtlClockGet();     // 40,000,000

    // -----------------------------------------------------------------------
    // 2. Enable peripherals
    // -----------------------------------------------------------------------
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);

    // -----------------------------------------------------------------------
    // 3. GPIO: PF2 = BLUE LED output
    // -----------------------------------------------------------------------
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_2);
    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 0);   // start LOW

    // -----------------------------------------------------------------------
    // 4. GPIO: SW1 = PF4 input with pull-up (active-low)
    // -----------------------------------------------------------------------
    GPIOPinTypeGPIOInput(GPIO_PORTF_BASE, GPIO_PIN_4);
    GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_4,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    // -----------------------------------------------------------------------
    // 5. GPIO: SW2 = PF0 input with pull-up.
    //    PF0 is locked by default on TM4C123 — must unlock before configuring.
    // -----------------------------------------------------------------------
    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR)  |= 0x01;    // allow changes to PF0
    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = 0;       // re-lock

    GPIOPinTypeGPIOInput(GPIO_PORTF_BASE, GPIO_PIN_0);
    GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_0,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    // -----------------------------------------------------------------------
    // 6. Timer0A: 32-bit periodic timer, interrupt on timeout
    // -----------------------------------------------------------------------
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);

    g_ui32Frequency_x10 = FREQ_START_x10;   // 5.0 Hz
    UpdateTimerPeriod();                    // load value

    TimerIntRegister(TIMER0_BASE, TIMER_A, Timer0AIntHandler);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);

    // -----------------------------------------------------------------------
    // 7. GPIO Port F interrupt — both SW1 (falling) and SW2 (rising)
    // -----------------------------------------------------------------------
    GPIOIntRegister(GPIO_PORTF_BASE, GPIOPortFIntHandler);
    GPIOIntTypeSet(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_FALLING_EDGE);  // SW1
    GPIOIntTypeSet(GPIO_PORTF_BASE, GPIO_PIN_0, GPIO_RISING_EDGE);   // SW2
    GPIOIntEnable(GPIO_PORTF_BASE, GPIO_INT_PIN_4 | GPIO_INT_PIN_0);
    IntPrioritySet(INT_GPIOF, 0);           // button priority higher than timer
    IntEnable(INT_GPIOF);

    // -----------------------------------------------------------------------
    // 8. Global interrupts + start timer
    // -----------------------------------------------------------------------
    IntMasterEnable();
    TimerEnable(TIMER0_BASE, TIMER_A);

    // -----------------------------------------------------------------------
    // 9. Main loop — everything is interrupt-driven
    // -----------------------------------------------------------------------
    while (1) { 
        
    }
}

// ===========================================================================
// Timer0A ISR — toggles BLUE LED on PF2 every half period
// ===========================================================================
void Timer0AIntHandler(void)
{
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    if (GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_2))
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 0);   // LOW
    else
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 4);   // HIGH (bit 2)
}

// ===========================================================================
// GPIO Port F ISR — handles SW1 (falling) and SW2 (rising)
// ===========================================================================
void GPIOPortFIntHandler(void)
{
    // Read masked interrupt status to know which pin fired
    uint32_t status = GPIOIntStatus(GPIO_PORTF_BASE, true);

    // ---- SW1 (PF4) falling edge → increase frequency by 0.5 Hz ----
    if (status & GPIO_INT_PIN_4)
    {
        GPIOIntClear(GPIO_PORTF_BASE, GPIO_INT_PIN_4);

        if (g_ui32Frequency_x10 < FREQ_MAX_x10)
        {
            g_ui32Frequency_x10 += FREQ_STEP_x10;
            UpdateTimerPeriod();
        }
        // else: at 9.5 Hz, ignore further presses (saturate)
    }

    // ---- SW2 (PF0) rising edge → decrease frequency by 0.5 Hz ----
    if (status & GPIO_INT_PIN_0)
    {
        GPIOIntClear(GPIO_PORTF_BASE, GPIO_INT_PIN_0);

        if (g_ui32Frequency_x10 > FREQ_MIN_x10)
        {
            g_ui32Frequency_x10 -= FREQ_STEP_x10;
            UpdateTimerPeriod();
        }
        // else: at 0.5 Hz, ignore further releases (saturate)
    }
}
