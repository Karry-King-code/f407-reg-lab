/*
 * 节点 1 · 点亮 LED 灯（练习版 · 空白模板）
 *
 * 分支说明：practice 分支 = 你自己手敲的地方；
 *           main 分支的 main.c = 参考答案（卡壳先自己尽力，再参照）。
 *
 * 目标现象：LED1 亮 500ms → 灭 500ms → 无限循环
 *
 * 步骤骨架（细节全部自己查原理图和 RM0090）：
 *   ① 从原理图确认：LED1 接哪个引脚？什么电平点亮？
 *   ② 开该 GPIO 端口的时钟
 *   ③ 把该引脚配置成推挽输出
 *   ④ 做 500ms 的延时手段
 *   ⑤ 高 → 延时 → 低 → 延时，循环
 *
 * 迁移提醒（从 F103 过来必看）：
 *   - F4 没有 CRL/CRH，GPIO 配置寄存器换成了新名字（翻手册找）
 *   - GPIO 时钟使能不在 APB2ENR 了，搬到了另一个域（翻手册找）
 *   - 本板 LED 是【高电平点亮】（F103 板相反，以原理图为准）
 *   - 复位后芯片跑内部 HSI 16MHz（延时参数计算要用到这个频率）
 */
#include "stm32f4xx.h"
void delay_ms(uint32_t ms)
{
    uint32_t tick = SystemCoreClock / 8 / 1000;   /* 1ms 计数次数 */
    SysTick->LOAD = tick - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
    while (ms--)
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0)
        {
        }
    SysTick->CTRL = 0;
}

int main(void)
{
    SystemCoreClockUpdate();  

    /* ① 开 GPIOD 时钟：外设时钟使能寄存器
      用 |= 只改这一位，不碰其他位。 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    
    /* ③ 把 PD10 配成输出：MODER 每脚占 2 位，PD10 = 10号脚 × 2 = bit[21:20]，01=输出 */
    GPIOD->MODER &= ~(3UL << 20);   /* 先清零：3=0b11，把 [21:20] 两位旧值清掉 */
    GPIOD->MODER |=  (1UL << 20);   /* 再写 1：[21:20]=01 = 通用输出模式 */
	  GPIOD->MODER &= ~(3UL << 22);   
    GPIOD->MODER |=  (1UL << 22);   

    /* ③ 上电先灭：BSRR 写 BR10 = 输出低电平 -> 灭。原子操作 = 一步写完，无读改写 */    GPIOD->BSRR = GPIO_BSRR_BR10;

    while (1)
    { 
        GPIOD->BSRR  = GPIO_BSRR_BR10;   /* BSRR 写 BR10 = 输出低电平 -> 灭 */
        delay_ms(500);
        GPIOD->BSRR = GPIO_BSRR_BS10;   /* BSRR 写 BS10 = 输出高电平 -> 亮 */
        delay_ms(500);
			  GPIOD->BSRR  = GPIO_BSRR_BR11;   /* BSRR 写 BS10 = 输出低电平 -> 灭 */
        delay_ms(500);
        GPIOD->BSRR = GPIO_BSRR_BS11;   /* BSRR 写 BS10 = 输出高电平 -> 亮 */
        delay_ms(500);
    }
}
