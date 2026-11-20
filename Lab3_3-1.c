#include <stdint.h>
#include <stdbool.h>
#include "inc/tm4c123gh6pm.h"

#define SYS_CLK_HZ      16000000U
#define T1_PRESCALER    255U
#define TICK_CLK_HZ     (SYS_CLK_HZ / (T1_PRESCALER + 1U))

static volatile uint32_t g_last_capture = 0;
static volatile uint32_t g_period_ticks = 0;
static volatile bool g_has_capture = false;
static volatile float g_measured_freq = 0.0f;

static volatile uint8_t g_digit_val[3] = {0, 0, 0};
static volatile bool g_dot_pos[3] = {true, false, false};

/* Common cathode segment lookup: bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G */
static const uint8_t SEG_LUT[10] = {
    0x3F, /* 0 */
    0x06, /* 1 */
    0x5B, /* 2 */
    0x4F, /* 3 */
    0x66, /* 4 */
    0x6D, /* 5 */
    0x7D, /* 6 */
    0x07, /* 7 */
    0x7F, /* 8 */
    0x6F  /* 9 */
};

static void DelayMs(uint32_t ms)
{
    volatile uint32_t i;
    while (ms > 0U) {
        for (i = 0U; i < 2666U; i++) {
            __asm("    nop");
        }
        ms--;
    }
}

static void SetSegments(uint8_t pattern, bool dp_on)
{
    /* PC4=A, PC5=F, PC6=B, PC7=G */
    uint8_t pc_val = 0U;
    if ((pattern & 0x01U) != 0U) pc_val |= (1U << 4);
    if ((pattern & 0x20U) != 0U) pc_val |= (1U << 5);
    if ((pattern & 0x02U) != 0U) pc_val |= (1U << 6);
    if ((pattern & 0x40U) != 0U) pc_val |= (1U << 7);
    GPIO_PORTC_DATA_R = (GPIO_PORTC_DATA_R & ~0xF0U) | pc_val;

    /* PE2=C, PE1=DP */
    uint8_t pe_val = 0U;
    if ((pattern & 0x04U) != 0U) pe_val |= (1U << 2);
    if (dp_on)                   pe_val |= (1U << 1);
    GPIO_PORTE_DATA_R = (GPIO_PORTE_DATA_R & ~0x06U) | pe_val;

    /* PD3=D, PD2=E */
    uint8_t pd_val = 0U;
    if ((pattern & 0x08U) != 0U) pd_val |= (1U << 3);
    if ((pattern & 0x10U) != 0U) pd_val |= (1U << 2);
    GPIO_PORTD_DATA_R = (GPIO_PORTD_DATA_R & ~0x0CU) | pd_val;
}

static void SelectDigit(int idx)
{
    /* DIG1=PA2, DIG2=PA3, DIG3=PA4 (active high) */
    GPIO_PORTA_DATA_R &= ~((1U << 2) | (1U << 3) | (1U << 4));
    if (idx == 0) {
        GPIO_PORTA_DATA_R |= (1U << 2);
    } else if (idx == 1) {
        GPIO_PORTA_DATA_R |= (1U << 3);
    } else if (idx == 2) {
        GPIO_PORTA_DATA_R |= (1U << 4);
    }
}

static void UpdateDisplayDigits(float freq)
{
    if (freq < 0.0f) {
        freq = 0.0f;
    }
    if (freq > 99.9f) {
        freq = 99.9f;
    }

    uint32_t tenths = (uint32_t)(freq * 10.0f + 0.5f);
    if (tenths > 999U) {
        tenths = 999U;
    }

    /* Format XX.Y across DIG1, DIG2, DIG3 with decimal point on DIG2 */
    uint8_t d1 = (uint8_t)((tenths / 100U) % 10U);
    uint8_t d2 = (uint8_t)((tenths / 10U) % 10U);
    uint8_t d3 = (uint8_t)(tenths % 10U);

    g_digit_val[0] = d1;
    g_digit_val[1] = d2;
    g_digit_val[2] = d3;

    g_dot_pos[0] = false;
    g_dot_pos[1] = true;
    g_dot_pos[2] = false;
}

static void GPIO_7Seg_Init(void)
{
    SYSCTL_RCGCGPIO_R |= (1U << 0) | (1U << 2) | (1U << 3) | (1U << 4);
    while ((SYSCTL_PRGPIO_R & ((1U << 0) | (1U << 2) | (1U << 3) | (1U << 4)))
           != ((1U << 0) | (1U << 2) | (1U << 3) | (1U << 4))) {}

    /* PA2, PA3, PA4 outputs */
    GPIO_PORTA_DIR_R |= (1U << 2) | (1U << 3) | (1U << 4);
    GPIO_PORTA_DEN_R |= (1U << 2) | (1U << 3) | (1U << 4);
    GPIO_PORTA_DATA_R &= ~((1U << 2) | (1U << 3) | (1U << 4));

    /* PC4, PC5, PC6, PC7 outputs */
    GPIO_PORTC_DIR_R |= 0xF0U;
    GPIO_PORTC_DEN_R |= 0xF0U;
    GPIO_PORTC_DATA_R &= ~0xF0U;

    /* PD2, PD3 outputs */
    GPIO_PORTD_DIR_R |= (1U << 2) | (1U << 3);
    GPIO_PORTD_DEN_R |= (1U << 2) | (1U << 3);
    GPIO_PORTD_DATA_R &= ~((1U << 2) | (1U << 3));

    /* PE1, PE2 outputs */
    GPIO_PORTE_DIR_R |= (1U << 1) | (1U << 2);
    GPIO_PORTE_DEN_R |= (1U << 1) | (1U << 2);
    GPIO_PORTE_DATA_R &= ~((1U << 1) | (1U << 2));
}

static void Timer1A_Capture_PB4_Init(void)
{
    SYSCTL_RCGCGPIO_R |= (1U << 1);
    SYSCTL_RCGCTIMER_R |= (1U << 1);
    while ((SYSCTL_PRGPIO_R & (1U << 1)) == 0U) {}
    while ((SYSCTL_PRTIMER_R & (1U << 1)) == 0U) {}

    GPIO_PORTB_DIR_R &= ~(1U << 4);
    GPIO_PORTB_AFSEL_R |= (1U << 4);
    GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R & ~0x000F0000U) | 0x00070000U;
    GPIO_PORTB_DEN_R |= (1U << 4);

    TIMER1_CTL_R &= ~TIMER_CTL_TAEN;
    TIMER1_CFG_R = TIMER_CFG_16_BIT;
    TIMER1_TAMR_R = (TIMER_TAMR_TAMR_CAP | TIMER_TAMR_TACMR);
    TIMER1_CTL_R &= ~(TIMER_CTL_TAEVENT_M);
    TIMER1_CTL_R |= TIMER_CTL_TAEVENT_POS;

    TIMER1_TAPR_R = T1_PRESCALER;   /* 255 -> 16MHz / 256 = 62.5 kHz */
    TIMER1_TAILR_R = 0xFFFFU;

    TIMER1_ICR_R = TIMER_ICR_CAECINT;
    TIMER1_IMR_R |= TIMER_IMR_CAEIM;

    NVIC_EN0_R |= (1U << 21);
    TIMER1_CTL_R |= TIMER_CTL_TAEN;
}
static void SysTick_Scan_Init(void)
{
    /* Scan 3 digits every 2 ms (~166 Hz refresh across all 3 digits) */
    NVIC_ST_CTRL_R = 0U;
    NVIC_ST_RELOAD_R = (SYS_CLK_HZ / 500U) - 1U;
    NVIC_ST_CURRENT_R = 0U;
    NVIC_ST_CTRL_R = NVIC_ST_CTRL_CLK_SRC | NVIC_ST_CTRL_INTEN | NVIC_ST_CTRL_ENABLE;
}

void Timer1A_Handler(void)
{
    if ((TIMER1_MIS_R & TIMER_MIS_CAEMIS) != 0U) {
        TIMER1_ICR_R = TIMER_ICR_CAECINT;

        uint32_t now_val = (((uint32_t)(TIMER1_TAPS_R & 0xFFU)) << 16) |
                           ((uint32_t)(TIMER1_TAR_R & 0xFFFFU));

        if (g_has_capture) {
            uint32_t diff = 0U;
            if (g_last_capture >= now_val) {
                diff = g_last_capture - now_val;
            } else {
                diff = (0x00FFFFFFU - now_val) + g_last_capture + 1U;
            }
            if (diff > 0U) {
                g_period_ticks = diff;
            }
        } else {
            g_has_capture = true;
        }
        g_last_capture = now_val;
    }
}

void SysTick_Handler(void)
{
    static int digit_idx = 0;

    SelectDigit(-1);
    SetSegments(SEG_LUT[g_digit_val[digit_idx]], g_dot_pos[digit_idx]);
    SelectDigit(digit_idx);

    digit_idx++;
    if (digit_idx >= 3) {
        digit_idx = 0;
    }
}

int main(void)
{
    GPIO_7Seg_Init();
    Timer1A_Capture_PB4_Init();
    SysTick_Scan_Init();

    UpdateDisplayDigits(0.0f);

    while (1) {
        uint32_t ticks_snapshot = 0U;

        __asm("    cpsid i");
        ticks_snapshot = g_period_ticks;
        __asm("    cpsie i");

        if (ticks_snapshot > 0U) {
            float freq = (float)TICK_CLK_HZ / (float)ticks_snapshot;
            if ((freq >= 0.1f) && (freq <= 99.9f)) {
                g_measured_freq = freq;
                UpdateDisplayDigits(g_measured_freq);
            }
        }

        DelayMs(50U);
    }
}
