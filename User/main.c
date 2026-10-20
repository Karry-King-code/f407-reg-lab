/*
 * 节点 9 · DAC（寄存器版）—— 输出 + ADC 内部回读自测
 *
 * 硬件冲突说明：
 *   DAC_OUT1 = PA4，PA4 同时是 Flash 的 CS（节点五用过）。
 *   本节点 DAC 驱动 PA4 时，Flash 的 CS 会跟着 DAC 电压走——但 SPI1 没时钟，
 *   Flash 不会响应，安全。测完切回节点五代码即可恢复。
 *
 * 自测闭环（不用接线）：
 *   PA4 同时是 DAC1_OUT1 和 ADC1_IN4 —— 写 DAC，立刻用 ADC 读回同一根脚。
 *
 * 硬件事实（RM0090）：
 *   DAC 在 APB1（bit29），输出 = VREF+ × DHR / 4095，本板 VREF+ = 3.3V
 *   DAC1 通道使能 = DAC_CR.EN1；数据写 DHR12R1（12 位右对齐）
 *   PA4 必须配成模拟模式（MODER=11），DAC 和 ADC 共用这根脚
 *
 * 现象：串口每 500ms 打印三组：DAC设定值 vs ADC实测值，两者应接近
 */

#include "stm32f4xx.h"

/* ============ 串口 ============ */
static void uart1_send(uint8_t b) { while ((USART1->SR & USART_SR_TXE) == 0) { } USART1->DR = b; }
static void uart1_str(const char *s) { while (*s) uart1_send((uint8_t)*s++); }
static void uart1_u32(uint32_t v)
{
    char buf[12]; int i = 0;
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

/* ============ DAC1 ============ */
static void dac_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_DACEN;          /* DAC 在 APB1 bit29 */

    GPIOA->MODER |= (3UL << 8);                 /* PA4 = 模拟模式 11（DAC/ADC 共用） */

    DAC->CR = DAC_CR_EN1;                       /* 通道 1 使能，不用触发，不用缓冲 */
}

static void dac_set(uint16_t v)
{
    DAC->DHR12R1 = v;                           /* 12 位右对齐：Vout = 3.3V × v/4095 */
}

/* ============ ADC1（只为回读 PA4） ============ */
static void adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC->CCR = ADC_CCR_ADCPRE_0;                /* PCLK2/2 = 8MHz */
    ADC1->CR1 = 0;
    ADC1->SMPR2 |= (7UL << 12);                 /* 通道 4（PA4）采样 480 周期：bits14:12 */
    ADC1->SQR1 = 0;
    ADC1->CR2 = ADC_CR2_ADON;
    delay_ms(10);
}

static uint16_t adc_read_ch4(void)
{
    ADC1->SQR3 = 4;                             /* 通道 4 = PA4 */
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC)) { }
    return (uint16_t)ADC1->DR;
}

int main(void)
{
    uint16_t set, got;
    uint32_t mv_set, mv_got;

    uart_init();
    dac_init();
    adc_init();

    uart1_str("\r\n=== DAC LOOPBACK TEST ===\r\n");
    uart1_str("DAC_OUT1=PA4 (also ADC1_IN4)\r\n");

    while (1)
    {
        /* 三档循环：0V / 1.65V / 3.3V */
        for (set = 0; set < 4096; set = (uint16_t)(set + 2048))
        {
            dac_set(set);
            delay_ms(20);                       /* 等输出稳定 */
            got = adc_read_ch4();

            mv_set = (uint32_t)set * 3300UL / 4095UL;
            mv_got = (uint32_t)got * 3300UL / 4095UL;

            uart1_str("DAC_SET=");
            uart1_u32(set);
            uart1_str(" (");
            uart1_u32(mv_set);
            uart1_str("mV)   ADC_READ=");
            uart1_u32(got);
            uart1_str(" (");
            uart1_u32(mv_got);
            uart1_str("mV)\r\n");
        }
        uart1_str("---\r\n");
        delay_ms(500);
    }
}
