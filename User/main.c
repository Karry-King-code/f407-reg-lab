/*
 * 节点 4 · I2C 读写 EEPROM（AT24C02）—— 总线解锁版
 *
 * 诊断链：STAGE=6 + SR1=0x00（无任何标志）= 地址字节没被时钟出去 = 总线被卡住。
 * 两个嫌疑一起治：
 *   A. 从机（EEPROM）可能被之前推挽时期的失败传输拽住了状态机 → 上电先做
 *      "9 时钟 + STOP" 总线解锁仪式（I2C 行业标准救场动作）
 *   B. F4 官方勘误：模拟滤波器可致 BUSY 位锁死 → PE 关/开抖动 + 等 BUSY 落地
 */

#include "stm32f4xx.h"

volatile uint8_t g_stage = 0;
volatile uint8_t g_sr1   = 0;
volatile uint8_t g_sr2   = 0;

/* ============ 串口（节点三） ============ */

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

/* ============ I2C2 ============ */

static void i2c_delay(uint32_t n)            /* 粗略短延时（解锁仪式用） */
{
    while (n--) { __asm("nop"); }
}

/* 总线解锁：PB10 当 SCL 开漏输出，打 9 个时钟 + 造 STOP，顶出任何卡死的从机 */
static void i2c_bus_clear(void)
{
    uint32_t i;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    /* 两根线先当普通开漏 GPIO：SCL=PB10、SDA=PB11，都先放高 */
    GPIOB->MODER &= ~((3UL << 20) | (3UL << 22));            /* 00 = 输出（旧式编码） */
    GPIOB->MODER |=  (1UL << 20) | (1UL << 22);              /* 01 = 输出 */
    GPIOB->OTYPER |= (3UL << 10);                            /* 开漏 */
    GPIOB->BSRR = (3UL << 10);                               /* 两线放高（写 BS 位） */

    /* 若 SDA 被从机拽低：最多打 9 个 SCL 脉冲，直到 SDA 松手 */
    for (i = 0; i < 9 && !(GPIOB->IDR & (1UL << 11)); i++)
    {
        GPIOB->BSRR = (1UL << 10);                           /* SCL 低 */
        i2c_delay(200);
        GPIOB->BSRR = (1UL << 10) << 16;                     /* SCL 高 */
        i2c_delay(200);
    }

    /* 造一个 STOP：SCL 高时，SDA 由低到高 */
    GPIOB->BSRR = (1UL << 10);                               /* SCL 低 */
    i2c_delay(200);
    GPIOB->BSRR = (1UL << 11);                               /* SDA 低 */
    i2c_delay(200);
    GPIOB->BSRR = (1UL << 10) << 16;                         /* SCL 高 */
    i2c_delay(200);
    GPIOB->BSRR = (1UL << 11) << 16;                         /* SDA 抬高 = STOP */
    i2c_delay(200);
}

static void i2c_init(void)
{
    i2c_bus_clear();                          /* 先解锁总线 */

    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;

    GPIOB->MODER &= ~(3UL << 20);  GPIOB->MODER |= (2UL << 20);    /* PB10 复用 AF */
    GPIOB->MODER &= ~(3UL << 22);  GPIOB->MODER |= (2UL << 22);    /* PB11 复用 AF */
    GPIOB->OTYPER |= (3UL << 10);                                  /* 开漏（必须） */
    GPIOB->AFR[1] &= ~(0xFUL << 8);  GPIOB->AFR[1] |= (4UL << 8);  /* PB10 AF4 */
    GPIOB->AFR[1] &= ~(0xFUL << 12); GPIOB->AFR[1] |= (4UL << 12); /* PB11 AF4 */

    RCC->APB1RSTR |=  RCC_APB1RSTR_I2C2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C2RST;

    I2C2->CR2   = 16;
    I2C2->CCR   = 80;
    I2C2->TRISE = 17;

    /* F4 勘误舞步：PE 关 → 等 BUSY 落地（不落地就发 STOP 帮它）→ PE 开 → 再等 */
    I2C2->CR1 &= ~I2C_CR1_PE;
    {
        uint32_t t = 200000;
        while ((I2C2->SR2 & I2C_SR2_BUSY) && --t)
        {
            if (t == 100000)
                I2C2->CR1 |= I2C_CR1_STOP;
        }
    }
    I2C2->CR1 |= I2C_CR1_PE;
    {
        uint32_t t = 200000;
        while ((I2C2->SR2 & I2C_SR2_BUSY) && --t) { }
    }
}

static uint8_t i2c_wait(uint32_t flag)
{
    uint32_t t = 100000;
    while (!(I2C2->SR1 & flag) && --t) { }
    if (t == 0) {
        g_sr1 = I2C2->SR1;
        g_sr2 = I2C2->SR2 & 0x07;            /* 只留 MSL/BUSY/TRA 低三位 */
        I2C2->SR1 = 0;
        I2C2->CR1 |= I2C_CR1_STOP;
        return 0;
    }
    return 1;
}

static uint8_t i2c_write_byte(uint8_t mem, uint8_t data)
{
    I2C2->CR1 |= I2C_CR1_START;
    g_stage = 1;
    if (!i2c_wait(I2C_SR1_SB)) return 0;
    (void)I2C2->SR1;
    I2C2->DR = 0xA0;
    g_stage = 2;
    if (!i2c_wait(I2C_SR1_ADDR)) return 0;
    (void)I2C2->SR1; (void)I2C2->SR2;
    I2C2->DR = mem;
    g_stage = 3;
    if (!i2c_wait(I2C_SR1_TXE)) return 0;
    I2C2->DR = data;
    g_stage = 4;
    if (!i2c_wait(I2C_SR1_BTF)) return 0;
    I2C2->CR1 |= I2C_CR1_STOP;
    g_stage = 0;
    return 1;
}

static uint8_t i2c_read_byte(uint8_t mem, uint8_t *out)
{
    I2C2->CR1 |= I2C_CR1_ACK;
    I2C2->CR1 |= I2C_CR1_START;
    g_stage = 5;
    if (!i2c_wait(I2C_SR1_SB)) return 0;
    (void)I2C2->SR1;
    I2C2->DR = 0xA0;
    g_stage = 6;
    if (!i2c_wait(I2C_SR1_ADDR)) return 0;
    (void)I2C2->SR1; (void)I2C2->SR2;
    I2C2->DR = mem;
    g_stage = 7;
    if (!i2c_wait(I2C_SR1_TXE)) return 0;

    I2C2->CR1 |= I2C_CR1_START;
    g_stage = 8;
    if (!i2c_wait(I2C_SR1_SB)) return 0;
    (void)I2C2->SR1;
    I2C2->DR = 0xA1;
    g_stage = 9;
    if (!i2c_wait(I2C_SR1_ADDR)) return 0;
    I2C2->CR1 &= ~I2C_CR1_ACK;
    (void)I2C2->SR1; (void)I2C2->SR2;
    g_stage = 10;
    if (!i2c_wait(I2C_SR1_RXNE)) return 0;
    I2C2->CR1 |= I2C_CR1_STOP;
    *out = I2C2->DR;
    g_stage = 0;
    return 1;
}

int main(void)
{
    uint8_t rd = 0;
    uint8_t ok;

    uart_init();
    i2c_init();

    uart1_str("\r\n=== I2C2 AT24C02 TEST ===\r\n");

    ok = i2c_write_byte(0x00, 0x5A);
    ok &= i2c_read_byte(0x00, &rd);

    uart1_str("WR=0x5A  RD=0x");
    uart1_hex(rd);
    if (ok && rd == 0x5A) {
        uart1_str("  PASS\r\n");
    } else {
        uart1_str("  FAIL  STAGE=0x");
        uart1_hex(g_stage);
        uart1_str("  SR1=0x");
        uart1_hex(g_sr1);
        uart1_str("  SR2=0x");
        uart1_hex(g_sr2);
        uart1_str("\r\n");
    }

    while (1) { }
}
