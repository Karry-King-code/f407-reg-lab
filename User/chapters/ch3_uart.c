/*
 * 节点 3 · 串口收发（练习版 · 空白模板）
 *
 * 硬件事实（原理图第 3/7 页已核实）：
 *   USART1：TX = PA9 → 板上 CH340N 的 RX（电脑收）；RX = PA10 ← CH340N 的 TX（电脑发）
 *   你插着的 USB>TTL 口就是串口，不需要任何额外接线。串口助手设置：115200-8-N-1
 *
 * 目标现象（版本 A）：
 *   电脑串口助手发什么字节，板子原样发回什么（echo 回显）
 *   版本 B（进阶）：中断接收 + 环形缓冲（F103 铁律：轮询会丢字节）
 *
 * 步骤骨架：
 *   ① 开两个时钟（串口跨两条总线！）：GPIOA = AHB1ENR bit0，USART1 = APB2ENR bit4
 *   ② PA9/PA10 配"复用"：MODER 每脚 2 位写 10（不是 01 输出了！引脚让给外设）
 *      再在 AFRH 里选 AF7（串口功能）：PA9 → [7:4]=0111，PA10 → [11:8]=0111
 *   ③ 波特率：BRR = 0x8B（16MHz ÷ 16 ÷ 115200 = 8.6875 → 整数 8、小数 0.6875×16≈11）
 *   ④ CR1 同一次写 UE(bit13) + TE(bit3) + RE(bit2)
 *   ⑤ 循环：等 RXNE 收一字节 → 等 TXE 发回同一字节（echo）
 *
 * 新概念：复用功能（MODER=10 + AFRH 选 AF7）、波特率除法（BRR）、状态位轮询（TXE/RXNE）
 * 教材：总文档"节点三 · 串口"章（每步都有截图和推导）
 */

#include "stm32f4xx.h"

#define BUF_LEN  64
volatile uint8_t  rbuf[BUF_LEN];
volatile uint16_t rhead = 0, rtail = 0;      /* 写指针 / 读指针 */

void USART1_IRQHandler (void)
{
 if(USART1->SR & USART_SR_RXNE)
 {
  rbuf[rhead] = USART1->DR;
	rhead = (rhead + 1)% BUF_LEN;
 }
}
int main(void)
{
    SystemCoreClockUpdate();

    /* ① 开两个时钟：GPIOA（AHB1ENR bit0）+ USART1（APB2ENR bit4） */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;     /* ① GPIOA 时钟（AHB1 bit0） */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;    /* ① USART1 时钟（APB2 bit4） */
	
    /* ② PA9/PA10：MODER [19:18]=10、[21:20]=10（复用）；AFRH [7:4]=0111、[11:8]=0111（AF7） */
    GPIOA->MODER &= ~(3UL << 18); 
    GPIOA->MODER |=  (2UL << 18);	
    GPIOA->MODER &= ~(3UL << 20);         
    GPIOA->MODER |=  (2UL << 20);          /* 再写 01 */
	
	  GPIOA->AFR[1] &= ~(0xFUL << 4);  GPIOA->AFR[1] |= (7UL << 4);   /* PA9 = AF7 */
    GPIOA->AFR[1] &= ~(0xFUL << 8);  GPIOA->AFR[1] |= (7UL << 8);   /* PA10 = AF7 */
	
    /* ③ USART1->BRR = 0x8B */
	  USART1->BRR = 0x8B;
    /* ④ USART1->CR1：UE + TE + RE */
    USART1->CR1 |= USART_CR1_UE;
    USART1->CR1 |= USART_CR1_TE | USART_CR1_RE;
	  USART1->CR1 = USART_CR1_RXNEIE;
		
    while (1)
    {
        /* ⑤ if (SR 有 RXNE) { c = DR; 等 TXE; DR = c; } */
			if(rhead != rtail)
			{
				uint8_t c = rbuf[rtail];
				rtail = (rtail + 1) % BUF_LEN;
				while (!(USART1->SR & USART_SR_TXE)) { }
				USART1->DR = c;             
			}
			  }
}