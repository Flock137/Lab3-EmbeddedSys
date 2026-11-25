### Exercise 3 write-up 

- Include all necessary libraries
- Call some functions needed before main()
- Initiate global variables: Clock, Frequency, Min/Max/Step of Frequency
- Initiate starting state 
- 7-segment helper, similar to lab 2: Define port masks, segment table, write function, digit selections 
- Update Timer0A load value from g_ui32Frequency_x10 (From Exercise 2)
- main()
    - Set system clock
    - Enable peripheral 
    - Buttons: PF2 output (blue LED), PF4/PF0 inputs (buttons)
    - Display output and PB4 as T1CCP0
    - LED blinks periodically (Timer0A — Exercise 2 periodic blink)
    - GPIOF Interrupts as SW1 falling, SW2 rising
    - Enable peripherals for GPIO and Timer1
    - Configure PB4 as a timer input (T1CCP0)
    - Configure Timer1 as a 32-bit edge-time capture timer
    - Initiate SysTick
- Other functions other than main will be described in other sections below. They are interrupt service routines (ISRs)

### ISRs 

```c
// These four: 
void Timer0AIntHandler(void);   // Exercise 2: blink PF2
void GPIOPortFIntHandler(void); // Exercise 2: SW1/SW2 frequency control
void Timer1AIntHandler(void);   // Exercise 3: capture PF2 period
void SysTickIntHandler(void);   // Exercise 3: 7-seg multiplex
```

#### `Timer0AIntHandler`
```c
void Timer0AIntHandler(void)
{
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    if (GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_2))
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 0);
    else
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 4);
}
```

This is the interrupt that would toggle the blue LED.


#### `GPIOPortFIntHandler`
```c
void GPIOPortFIntHandler(void)
{
    uint32_t status = GPIOIntStatus(GPIO_PORTF_BASE, true);

    if (status & GPIO_INT_PIN_4)
    { // SW1 falling → +0.5 Hz
        GPIOIntClear(GPIO_PORTF_BASE, GPIO_INT_PIN_4);
        if (g_ui32Frequency_x10 < FREQ_MAX_x10)
        {
            g_ui32Frequency_x10 += FREQ_STEP_x10;
            UpdateTimerPeriod();
        }
    }
    if (status & GPIO_INT_PIN_0)
    { // SW2 rising → −0.5 Hz
        GPIOIntClear(GPIO_PORTF_BASE, GPIO_INT_PIN_0);
        if (g_ui32Frequency_x10 > FREQ_MIN_x10)
        {
            g_ui32Frequency_x10 -= FREQ_STEP_x10;
            UpdateTimerPeriod();
        }
    }
}
```

- SW1 on PF4, falling edge → increase frequency by 0.5 Hz.
- SW2 on PF0, rising edge → decrease frequency by 0.5 Hz.



#### `Timer1AIntHandler`

```c
// These two below are global.
volatile uint32_t Point1, Point2;
volatile float Frequency;

void Timer1AIntHandler(void)
{
    TimerIntClear(TIMER1_BASE, TIMER_CAPA_EVENT);
    uint32_t current = TimerValueGet(TIMER1_BASE, TIMER_A);

    Point1 = Point2;
    Point2 = TimerValueGet(TIMER1_BASE, TIMER_A);

    uint32_t diff;

    if (Point2 > Point1)
    {
        Frequency = (float)g_ui32Clock / (Point2 - Point1);
    }
    if (Point2 < Point1)
    {
        Frequency = (float)g_ui32Clock / (16777216 + Point2 - Point1);
    }

    uint32_t f10 = (Frequency * 10U);
    if (f10 < 5U)
        f10 = 5U;
    if (f10 > 95U)
        f10 = 95U;
    g_ui8FreqInt = (uint8_t)(f10 / 10U);
    g_ui8FreqTenth = (uint8_t)(f10 % 10U);
}
```

- This is the Capture mode handler
- The function is calculating the frequency digits to be displayed on the screens
- Limits: 0.5 Hz to 9.5 Hz.


#### `SysTickIntHandler`

```c
void SysTickIntHandler(void)
{
    static uint8_t d = 0;
    digit_select(0); // all off to avoid ghosting

    if (d == 0)
    {
        write_segments(SEG_TABLE[g_ui8FreqInt] | 0x80); // "X."
        digit_select(1);
    }
    else if (d == 1)
    {
        write_segments(SEG_TABLE[g_ui8FreqTenth]); // "Y"
        digit_select(2);
    }
    else
    {
        write_segments(SEG_TABLE[0]);
        digit_select(3);
    }
    d = (d + 1) % 3;
}
```

- For displaying the digits on the screens. Since you can't turn everything all at once, there need to be a SysTick of 10ms between each of the screen. 


### Configure Timer1A in capture mode 

```c
TimerConfigure(TIMER1_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME_UP);
TimerControlEvent(TIMER1_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);
TimerLoadSet(TIMER1_BASE, TIMER_A, 0xFFFFFFFF);
TimerEnable(TIMER1_BASE, TIMER_A);
```

Line by line:

- `TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME_UP`

    - `TIMER_CFG_A_CAP_TIME_UP` means Timer A is in edge-time capture mode, counting up.

    - `TIMER_CFG_SPLIT_PAIR` splits the timer block into two independent 16-bit timers.

    - Important: This means Timer1A is configured as a 16-bit timer, not a 32-bit timer. This is a problem for Exercise 3, explained later.

- `TimerControlEvent(TIMER1_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);`

    - Capture on the rising edge of PB4.

    - Since PF2 is connected to PB4, every rising edge of the LED waveform is captured.

- `TimerLoadSet(TIMER1_BASE, TIMER_A, 0xFFFFFFFF);`

    - Sets the timer load value. Intended to make the counter free-run over its full range.

- `TimerEnable(TIMER1_BASE, TIMER_A);`

    - Starts Timer1A counting.


### Enable the capture interrupt

```c
TimerIntRegister(TIMER1_BASE, TIMER_A, Timer1AIntHandler);
TimerIntEnable(TIMER1_BASE, TIMER_CAPA_EVENT);
IntEnable(INT_TIMER1A);
```
- `TimerIntRegister` installs `Timer1AIntHandler` as the ISR for Timer1A.

- `TimerIntEnable()` enables the Timer A capture-event interrupt.

- `IntEnable(INT_TIMER1A)` enables the Timer1A interrupt in the NVIC.

- Later, `IntMasterEnable()` globally enables interrupts.

When a rising edge occurs on PB4, the timer hardware captures the current count and raises the capture interrupt.



























