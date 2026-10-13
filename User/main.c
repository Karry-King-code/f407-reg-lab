/*
 * 节点1 · 点亮 LED 灯（寄存器版）
 *
 * 硬件事实（原理图第3页 + 第7页引脚分配表，2026-10-13 双源核实）：
 *   LED1 = PD10，接法：PD10 -> 4.7K 电阻 -> LED -> GND，高电平点亮
 *   LED2 = PD11、LED3 = PD12（本节点只用 LED1）
 *
 * 时钟：复位默认 HSI 16MHz（SystemInit 不配 PLL），SysTick 按 16MHz 计算
 */

#include "stm32f4xx.h"

#define LED1_PIN       (1U << 10)          /* PD10 */

/* 毫秒延时：SysTick 轮询，1ms = 16000 个时钟（16MHz HSI） */
static void delay_ms(uint32_t ms)
{
    SysTick->LOAD = 16000U - 1U;           /* 重装值：数 16000 个时钟 = 1ms */
    SysTick->VAL  = 0U;                    /* 清当前值，保证从满值开始数 */
    SysTick->CTRL = 5U;                    /* bit0=开启 bit2=时钟源选HCLK 不开中断 */
    while (ms-- > 0U) {
        while ((SysTick->CTRL & (1U << 16)) == 0U) { }  /* 等 COUNTFLAG 置 1 */
    }
    SysTick->CTRL = 0U;                    /* 关闭 SysTick */
}

int main(void)
{
    /* 第一步：开 GPIOD 时钟。F4 的 GPIO 挂在 AHB1 上，GPIOD 是 bit3 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;

    /* 第二步：PD10 配成输出。MODER10 是 MODER 寄存器的 [21:20] 两位
       00=输入 01=输出 10=复用 11=模拟，这里填 01 */
    GPIOD->MODER &= ~(3U << 20);
    GPIOD->MODER |=  (1U << 20);

    /* OTYPER 复位默认 0 = 推挽，OSPEEDR 默认低速，够用不用改 */

    while (1)
    {
        GPIOD->BSRR = LED1_PIN;            /* 置 1：LED1 亮 */
        delay_ms(500);
        GPIOD->BSRR = (LED1_PIN << 16);    /* 写 BSRR 高 16 位：清 0，LED1 灭 */
        delay_ms(500);
    }
}
