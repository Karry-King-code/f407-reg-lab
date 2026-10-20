/*
 * 节点 8 · ADC（寄存器版）—— 读芯片内部温度 + 内部基准
 *
 * 不用接任何线！STM32F407 内置：
 *   ADC1_IN16 = 芯片温度传感器（手摸芯片温度上升，读数变大）
 *   ADC1_IN17 = VREFINT 内部基准 1.21V（验证 ADC 精度用）
 *
 * 硬件事实（RM0090）：
 *   ADC 时钟 = PCLK2 / 分频（CCR.ADCPRE），APB2=16MHz → /2 = 8MHz（上限 36MHz）
 *   分辨率 12 位：0~4095 对应 0~VDDA(3.3V)
 *   温度换算：T = (V25 − Vsense) / Avg_Slope + 25，V25≈0.76V，Avg_Slope≈2.5mV/°C
 *   温度传感器/VREFINT 必须开 CR2.TSVREFE，且采样时间要够长（≥10us，这里 480 周期≈60us）
 *
 * 现象：串口每 500ms 打印一次温度；手指按住芯片 10 秒，温度明显上升
 */

#include "stm32f4xx.h"

/* ============ 串口（节点三） ============ */
static void uart1_send(uint8_t b)
{
    while ((USART1->SR & USART_SR_TXE) == 0) { }
    USART1->DR = b;
}

static void uart1_str(const char *s)
{
    while (*s) uart1_send((uint8_t)*s++);
}

static void uart1_u32(uint32_t v)
{
    char buf[12];
    int i = 0;
    if (v == 0) { uart1_send('0'); return; }
    while (v) { buf[i++] = (char)('0' + v % 10U); v /= 10U; }
    while (i) uart1_send((uint8_t)buf[--i]);
}

static void uart_init(void)
{
    SystemCoreClockUpdate();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    GPIOA->MODER &= ~(3UL << 18);  GPIOA->MODER |= (2UL << 18);
    GPIOA->MODER &= ~(3UL << 20);  GPIOA->MODER |= (2UL << 20);
    GPIOA->AFR[1] &= ~(0xFUL << 4);  GPIOA->AFR[1] |= (7UL << 4);
    GPIOA->AFR[1] &= ~(0xFUL << 8);  GPIOA->AFR[1] |= (7UL << 8);
    USART1->BRR = 0x8B;
    USART1->CR1 |= USART_CR1_UE;
    USART1->CR1 |= USART_CR1_TE | USART_CR1_RE;
}

static void delay_ms(uint32_t ms)
{
    uint32_t tick = SystemCoreClock / 8 / 1000;
    SysTick->LOAD = tick - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
    while (ms--)
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0) { }
    SysTick->CTRL = 0;
}

/* ============ ADC1 ============ */
static void adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    ADC->CCR = ADC_CCR_ADCPRE_0;          /* PCLK2/2 = 8MHz（ADC 时钟上限 36MHz） */

    ADC1->CR1 = 0;                         /* 12 位、单通道、右对齐 */
    /* 通道 16/17 采样时间 480 周期（SMP=111） */
    ADC1->SMPR1 |= (7UL << 18) | (7UL << 21);
    ADC1->SQR1 = 0;                        /* 序列长度 1 */

    ADC1->CR2 = ADC_CR2_ADON;              /* 开 ADC */
    delay_ms(10);                          /* 稳定时间 */
    /* ★TSVREFE 在 F4 的 ADC_CCR bit23（F103 在 ADC_CR2！RM0090 p415 原文） */
    ADC->CCR |= (1UL << 23);
    delay_ms(10);                          /* 传感器启动时间 */
}

static uint16_t adc_read(uint8_t ch)
{
    ADC1->SQR3 = ch;                       /* 序列第 1 个 = 通道 ch */
    ADC1->CR2 |= ADC_CR2_SWSTART;          /* 软件启动转换 */
    while (!(ADC1->SR & ADC_SR_EOC)) { }   /* 等转换完成 */
    return (uint16_t)ADC1->DR;             /* 读 DR 自动清 EOC */
}

int main(void)
{
    uint16_t raw_t, raw_vref;
    uint32_t mv_t, mv_vref, temp_x10;

    uart_init();
    adc_init();

    uart1_str("\r\n=== ADC INTERNAL TEST ===\r\n");
    uart1_str("(touch the chip, temperature rises)\r\n");

    while (1)
    {
        raw_t    = adc_read(16);           /* 通道 16 = 温度传感器 */
        raw_vref = adc_read(17);           /* 通道 17 = VREFINT */

        mv_t    = (uint32_t)raw_t    * 3300UL / 4095UL;
        mv_vref = (uint32_t)raw_vref * 3300UL / 4095UL;

        /* T(°C) = (V25 - Vsense)/Avg_Slope + 25
           整数运算：T*10 = (760 - mV) * 4 + 250 */
        temp_x10 = (uint32_t)((760 - (int32_t)mv_t) * 4 + 250);

        uart1_str("TEMP_RAW=");
        uart1_u32(raw_t);
        uart1_str("  mV=");
        uart1_u32(mv_t);
        uart1_str("  T=");
        uart1_u32(temp_x10 / 10);
        uart1_send('.');
        uart1_u32(temp_x10 % 10);
        uart1_str("C   VREF_RAW=");
        uart1_u32(raw_vref);
        uart1_str("  mV=");
        uart1_u32(mv_vref);
        uart1_str("\r\n");

        delay_ms(500);
    }
}
