/*
 * 节点 2 · 按键控制 LED（练习版 · 空白模板）
 *
 * 硬件事实（原理图第 3/7 页已核实，坐标+视觉双源）：
 *   KEY1 = PD13、KEY2 = PD14、KEY3 = PD15
 *   电路：外部 10K 上拉到 3V3，按下接地 —— 空闲=高电平，按下=低电平（低有效）
 *
 * 目标现象：
 *   版本 A（先做）：按住 KEY1 → LED1 亮；松开 → LED1 灭（电平跟随）
 *   版本 B（进阶）：按一下 LED1 翻转一次（要做消抖，F103 节点三的同款思路）
 *
 * 步骤骨架：
 *   ① 开 GPIOD 时钟（同节点一）
 *   ② PD13 配成输入：MODER 每脚 2 位，PD13 = 13号脚 × 2 = bit[27:26]，输入 = 00
 *      （板上已有外部 10K 上拉，不需要再配上下拉，PUPDR 保持 00 即可）
 *   ③ LED1（PD10）按节点一配好输出，delay_ms 从 chapters/ch1_led.c 原样搬
 *   ④ 循环里：读按键电平 → 决定 LED1 亮灭
 *
 * 新寄存器只有一个：GPIOx_IDR（RM0090 中文版第 285 页）——引脚真实电平从这里读
 *
 * 迁移提醒：
 *   - 读电平用 IDR 不是 ODR（ODR 是输出锁存器，IDR 才是引脚现在的真实电平）
 *   - 输入 = MODER 两位写 00（复位默认就是 00，但显式清一遍是好习惯）
 *   - 本板按键低电平有效 —— 这次 F103 的习惯刚好是对的（以原理图为准，不是巧合）
 */

#include "stm32f4xx.h"

void delay_ms(uint32_t ms)
{
    uint32_t tick = SystemCoreClock / 8 / 1000;
    SysTick->LOAD = tick - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
    while (ms--)
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0)
        {
        }
    SysTick->CTRL = 0;
}

uint8_t key_pressed(void)
{
    static uint8_t last = 1;                     /* 上次状态（static 跨调用记忆） */
    uint8_t now = (GPIOD->IDR & GPIO_IDR_ID13) ? 1 : 0;   /* 读 IDR bit13：0=按下 1=松开 */
    uint8_t evt  = 0;

    if (last == 1 && now == 0)                   /* 1 -> 0 = 按下沿 */
    {
        delay_ms(10);                            /* 按下消抖 */
        if ((GPIOD->IDR & GPIO_IDR_ID13) == 0)    /* 抖完仍是 0 = 真按下 */
        {
            evt = 1;
            while ((GPIOD->IDR & GPIO_IDR_ID13) == 0)
            {
            }                                    /* 等松手 */
            delay_ms(10);                        /* 释放消抖 */
        }
    }
    last = now;
    return evt;
}

int main(void)
{
	   uint8_t  running = 0;
	   uint16_t slice   = 0;
	   SystemCoreClockUpdate(); 
    /* ① 开 GPIOD 时钟 */
     RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    /* ② PD13 配输入：清 [27:26]（输入=00，清完不用写1） */
     GPIOD->MODER &= ~(3UL << 26);                            
    /* ③ PD10 配输出 + 上电先灭（节点一的代码） */
     GPIOD->MODER &= ~(3UL << 20);               
     GPIOD->MODER |=  (1UL << 20);
	   GPIOD->BSRR = GPIO_BSRR_BR10;
    while (1)
    {
        /* ④ 读 IDR 的 bit13：=0 按下（LED1 亮），=1 松开（LED1 灭） */
			 if (key_pressed())                       /* 每片开头照看按键 */
        {
            running = !running;                  /* 开 <-> 停 */
            if (running == 0)
                GPIOD->BSRR = GPIO_BSRR_BR10;    /* 停止：灯灭 */
        }
        if (running)
        {
            if (++slice >= 50)                   /* 50 片 x 10ms = 500ms */
            {
                slice = 0;
                GPIOD->ODR ^= GPIO_ODR_OD10;     /* 翻转：ODR 异或 bit13（本工程无中断，读改写安全） */
            }
        }
        delay_ms(10);                            /* 节拍器 */
    }
    
}
