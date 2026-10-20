/*
 * 回测：硬件 I2C2 外设 + STOP 等 BUSY + ACK 轮询
 * 目的：证明硬件外设可用，此前失败是自己代码缺两点。
 */

#include "stm32f4xx.h"

void uart1_send(uint8_t b) { while ((USART1->SR & USART_SR_TXE) == 0) { } USART1->DR = b; }
void uart1_str(const char *s) { while (*s) uart1_send((uint8_t)*s++); }
void uart1_hex(uint8_t v) { const char h[] = "0123456789ABCDEF"; uart1_send((uint8_t)h[v >> 4]); uart1_send((uint8_t)h[v & 0x0F]); }

void uart_init(void)
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

static void busy_wait_ms(void)        /* 约 2ms 粗延时 */
{
    volatile uint32_t n = 60000;
    while (n--) { __asm("nop"); }
}

static uint8_t i2c_wait_busy(void)    /* 等 BUSY 清零（STOP 真正完成） */
{
    uint32_t t = 200000;
    while ((I2C2->SR2 & I2C_SR2_BUSY) && --t) { }
    return t ? 1 : 0;
}

static uint8_t i2c_wait_flag(uint32_t flag)
{
    uint32_t t = 200000;
    while (!(I2C2->SR1 & flag) && --t) { }
    return t ? 1 : 0;
}

static void i2c_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;

    GPIOB->MODER &= ~(3UL << 20);  GPIOB->MODER |= (2UL << 20);
    GPIOB->MODER &= ~(3UL << 22);  GPIOB->MODER |= (2UL << 22);
    GPIOB->OTYPER |= (3UL << 10);                       /* 开漏 */
    GPIOB->AFR[1] &= ~(0xFUL << 8);  GPIOB->AFR[1] |= (4UL << 8);
    GPIOB->AFR[1] &= ~(0xFUL << 12); GPIOB->AFR[1] |= (4UL << 12);

    RCC->APB1RSTR |=  RCC_APB1RSTR_I2C2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C2RST;

    I2C2->CR2   = 16;
    I2C2->CCR   = 80;
    I2C2->TRISE = 17;
    I2C2->CR1  |= I2C_CR1_PE;

    /* 等外设落地（F4 已知：PE 置位后 BUSY 可能滞留一会儿） */
    {
        uint32_t t = 200000;
        while ((I2C2->SR2 & I2C_SR2_BUSY) && --t) { }
    }
}

/* 写一个字节：流程结束等 BUSY 清零 */
static uint8_t hw_write(uint8_t mem, uint8_t data)
{
    if (!i2c_wait_busy()) return 0;                    /* ★前一道门：总线空闲才开工 */

    I2C2->CR1 |= I2C_CR1_START;
    if (!i2c_wait_flag(I2C_SR1_SB)) return 0;
    (void)I2C2->SR1;
    I2C2->DR = 0xA0;
    if (!i2c_wait_flag(I2C_SR1_ADDR)) return 0;
    (void)I2C2->SR1; (void)I2C2->SR2;
    I2C2->DR = mem;
    if (!i2c_wait_flag(I2C_SR1_TXE)) return 0;
    I2C2->DR = data;
    if (!i2c_wait_flag(I2C_SR1_BTF)) return 0;
    I2C2->CR1 |= I2C_CR1_STOP;
    if (!i2c_wait_busy()) return 0;                    /* ★后一道门：等 STOP 真正落地 */
    return 1;
}

/* ACK 轮询：反复敲门直到芯片写完（= 重新应答） */
static void hw_wait_ready(void)
{
    uint16_t t = 100;
    while (t--)
    {
        if (!i2c_wait_busy()) return;
        I2C2->CR1 |= I2C_CR1_START;
        if (!i2c_wait_flag(I2C_SR1_SB)) { I2C2->CR1 |= I2C_CR1_STOP; return; }
        (void)I2C2->SR1;
        I2C2->DR = 0xA0;
        if (i2c_wait_flag(I2C_SR1_ADDR)) {
            (void)I2C2->SR1; (void)I2C2->SR2;
            I2C2->CR1 |= I2C_CR1_STOP;
            i2c_wait_busy();
            return;                                    /* 应答了 = 写完了 */
        }
        I2C2->CR1 |= I2C_CR1_STOP;
        i2c_wait_busy();
        busy_wait_ms();
    }
}

static uint8_t hw_read(uint8_t mem, uint8_t *out)
{
    if (!i2c_wait_busy()) return 0;

    I2C2->CR1 |= I2C_CR1_ACK;
    I2C2->CR1 |= I2C_CR1_START;
    if (!i2c_wait_flag(I2C_SR1_SB)) return 0;
    (void)I2C2->SR1;
    I2C2->DR = 0xA0;
    if (!i2c_wait_flag(I2C_SR1_ADDR)) return 0;
    (void)I2C2->SR1; (void)I2C2->SR2;
    I2C2->DR = mem;
    if (!i2c_wait_flag(I2C_SR1_TXE)) return 0;
    if (!i2c_wait_flag(I2C_SR1_BTF)) return 0;         /* ★等字节真正发完再重复起始 */

    I2C2->CR1 |= I2C_CR1_START;
    if (!i2c_wait_flag(I2C_SR1_SB)) return 0;
    (void)I2C2->SR1;
    I2C2->DR = 0xA1;
    if (!i2c_wait_flag(I2C_SR1_ADDR)) return 0;
    I2C2->CR1 &= ~I2C_CR1_ACK;                         /* 单字节：NACK */
    (void)I2C2->SR1; (void)I2C2->SR2;
    if (!i2c_wait_flag(I2C_SR1_RXNE)) return 0;
    I2C2->CR1 |= I2C_CR1_STOP;
    *out = I2C2->DR;
    i2c_wait_busy();
    return 1;
}

int main(void)
{
    uint8_t rd = 0;
    uint8_t w1, r1;

    uart_init();
    i2c_init();

    uart1_str("\r\n=== AT24C02 HW-I2C2 RETEST ===\r\n");

    w1 = hw_write(0x00, 0x5A);
    hw_wait_ready();                                   /* ACK 轮询（替代死等） */
    r1 = hw_read(0x00, &rd);

    uart1_str("W:"); uart1_hex(w1);
    uart1_str("  R:"); uart1_hex(r1);
    uart1_str("  RD=0x"); uart1_hex(rd);
    if (w1 && r1 && rd == 0x5A) uart1_str("  PASS(HW-I2C2 OK)\r\n");
    else                        uart1_str("  FAIL\r\n");

    while (1) { }
}
