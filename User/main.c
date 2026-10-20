/*
 * 节点 4 · I2C 读写 EEPROM（AT24C02）—— 位带版 + ACK 轮询修正
 *
 * 修正点：EEPROM 写入后有最长 5ms 的内部编程周期，期间对一切请求 NAK。
 * 必须写完 STOP 后轮询（START+地址敲门）直到它重新 ACK，才能进行读。
 *
 * 输出说明：
 *   W:xxx = 写路径三拍应答（器件地址/格子地址/数据，1=ACK 0=NAK）
 *   R:xxx = 读路径三拍应答（器件地址(假写)/器件地址(真读)/收数据无意义）
 *   RD=0x??  PASS/FAIL
 */

#include "stm32f4xx.h"

#define SCL_H()   (GPIOB->BSRR = (1UL << 10))
#define SCL_L()   (GPIOB->BSRR = (1UL << 10) << 16)
#define SDA_H()   (GPIOB->BSRR = (1UL << 11))
#define SDA_L()   (GPIOB->BSRR = (1UL << 11) << 16)
#define SDA_READ()  ((GPIOB->IDR >> 11) & 1U)

static void i2c_dly(void)
{
    volatile uint32_t n = 60;
    while (n--) { __asm("nop"); }
}

static void busy_dly(void)                /* 约 2ms 粗延时（轮询间隔用） */
{
    volatile uint32_t n = 60000;
    while (n--) { __asm("nop"); }
}

/* ============ 串口 ============ */

void uart1_send(uint8_t b)
{
    while ((USART1->SR & USART_SR_TXE) == 0) { }
    USART1->DR = b;
}

void uart1_str(const char *s)
{
    while (*s)
        uart1_send((uint8_t)*s++);
}

void uart1_hex(uint8_t v)
{
    const char h[] = "0123456789ABCDEF";
    uart1_send((uint8_t)h[v >> 4]);
    uart1_send((uint8_t)h[v & 0x0F]);
}

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

/* ============ I2C 位带 ============ */

static void i2c_gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    GPIOB->MODER &= ~((3UL << 20) | (3UL << 22));
    GPIOB->MODER |=  (1UL << 20) | (1UL << 22);     /* 01 = 输出 */
    GPIOB->OTYPER |= (3UL << 10);                   /* 开漏 */
    GPIOB->BSRR = (3UL << 10);                      /* 空闲双高 */
}

static void i2c_start(void)
{
    SDA_H(); i2c_dly();
    SCL_H(); i2c_dly();
    SDA_L(); i2c_dly();
    SCL_L(); i2c_dly();
}

static void i2c_stop(void)
{
    SDA_L(); i2c_dly();
    SCL_H(); i2c_dly();
    SDA_H(); i2c_dly();
}

static uint8_t i2c_tx(uint8_t b)          /* 返回 1=ACK */
{
    uint8_t i, ack;
    for (i = 0; i < 8; i++)
    {
        if (b & 0x80) SDA_H(); else SDA_L();
        b <<= 1;
        i2c_dly();
        SCL_H(); i2c_dly();
        SCL_L(); i2c_dly();
    }
    SDA_H();
    i2c_dly();
    SCL_H(); i2c_dly();
    ack = (SDA_READ() == 0);
    SCL_L(); i2c_dly();
    return ack;
}

static uint8_t i2c_rx(uint8_t ack)
{
    uint8_t i, b = 0;
    SDA_H();
    for (i = 0; i < 8; i++)
    {
        b <<= 1;
        SCL_H(); i2c_dly();
        if (SDA_READ()) b |= 1;
        SCL_L(); i2c_dly();
    }
    if (ack) SDA_L(); else SDA_H();
    i2c_dly();
    SCL_H(); i2c_dly();
    SCL_L(); i2c_dly();
    SDA_H();
    return b;
}

/* ============ AT24C02 协议层 ============ */

static uint8_t at24_write(uint8_t mem, uint8_t data,
                          uint8_t *a1, uint8_t *a2, uint8_t *a3)
{
    i2c_start();
    *a1 = i2c_tx(0xA0);
    *a2 = i2c_tx(mem);
    *a3 = i2c_tx(data);
    i2c_stop();
    return *a1 & *a2 & *a3;
}

static void at24_wait_ready(void)         /* 轮询直到设备写完重新 ACK */
{
    uint16_t t = 100;                     /* 最多 100 次 x 2ms = 200ms 预算（写周期典型 5ms） */
    while (t--)
    {
        i2c_start();
        if (i2c_tx(0xA0)) { i2c_stop(); return; }
        i2c_stop();
        busy_dly();
    }
}

static uint8_t at24_read(uint8_t mem, uint8_t *out,
                         uint8_t *a1, uint8_t *a2)
{
    i2c_start();
    *a1 = i2c_tx(0xA0);                   /* 假写拨指针 */
    *a2 = i2c_tx(mem);
    if (!(*a1 & *a2)) { i2c_stop(); return 0; }
    i2c_start();                          /* 重复起始 */
    *a2 = i2c_tx(0xA1);                   /* 真读 */
    *out = i2c_rx(0);                     /* 单字节 NACK */
    i2c_stop();
    return *a1 & *a2;
}

int main(void)
{
    uint8_t rd = 0;
    uint8_t w1, w2, w3, r1, r2;
    uint8_t w_ok, r_ok;

    uart_init();
    i2c_gpio_init();

    uart1_str("\r\n=== AT24C02 TEST (bitbang + ackpoll) ===\r\n");

    w_ok = at24_write(0x00, 0x5A, &w1, &w2, &w3);
    at24_wait_ready();                    /* ★关键修正：等芯片写完 */
    r_ok = at24_read(0x00, &rd, &r1, &r2);

    uart1_str("W:"); uart1_hex(w1); uart1_hex(w2); uart1_hex(w3);
    uart1_str("  R:"); uart1_hex(r1); uart1_hex(r2);
    uart1_str("  RD=0x");
    uart1_hex(rd);
    if (w_ok && r_ok && rd == 0x5A)
        uart1_str("  PASS\r\n");
    else
        uart1_str("  FAIL\r\n");

    while (1) { }
}
