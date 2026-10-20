/*
 * 节点 5 · SPI 读写 Flash（W25Q16）—— 寄存器版（硬件 SPI1）
 *
 * 硬件事实（原理图第 4/7 页已核实）：
 *   W25Q16 SPI Flash（2MB，JEDEC ID 应为 EF 40 15）
 *   CS=PA4（普通 GPIO 片选）、SCK=PA5、MISO=PA6、MOSI=PA7（硬件 SPI1，AF5）
 *   SPI 模式 0（CPOL=0 CPHA=0），片选低电平有效
 *
 * 功能：
 *   ① 读 JEDEC ID：发 0x9F 收 3 字节，应为 EF 40 15
 *   ② 扇区擦除（4KB）→ 页编程写入 "F407" → 读回比对 → PASS
 *
 * 新概念：SPI 是四线全双工（收发同时进行——发一个字节必然收一个字节），
 * 全双工的代价是"读也要发哑字节 0xFF 造时钟"。
 */

#include "stm32f4xx.h"

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

/* ============ SPI1 ============ */

#define CS_L()   (GPIOA->BSRR = (1UL << 4))
#define CS_H()   (GPIOA->BSRR = (1UL << 4) << 16)

static void spi1_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;     /* GPIOA 时钟（已在 uart_init 开过，幂等） */
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;      /* SPI1 时钟（APB2 bit12） */

    GPIOA->MODER &= ~(3UL << 8);   GPIOA->MODER |= (1UL << 8);    /* PA4 输出（CS） */
    GPIOA->MODER &= ~((3UL << 10) | (3UL << 12) | (3UL << 14));
    GPIOA->MODER |= (2UL << 10) | (2UL << 12) | (2UL << 14);      /* PA5/6/7 复用 10 */
    GPIOA->AFR[0] &= ~(0xFUL << 20); GPIOA->AFR[0] |= (5UL << 20); /* PA5 AF5 */
    GPIOA->AFR[0] &= ~(0xFUL << 24); GPIOA->AFR[0] |= (5UL << 24); /* PA6 AF5 */
    GPIOA->AFR[0] &= ~(0xFUL << 28); GPIOA->AFR[0] |= (5UL << 28); /* PA7 AF5 */

    CS_H();                                  /* 片选空闲高 */

    SPI1->CR1 = SPI_CR1_MSTR                 /* 主机 */
              | SPI_CR1_SSM | SPI_CR1_SSI    /* 软件管片选 + 内部 NSS 拉高 */
              ;                              /* BR=000：fPCLK2/2 = 8MHz；CPOL=0 CPHA=0 模式0 */
    SPI1->CR1 |= SPI_CR1_SPE;                /* 使能（最后开） */
}

static uint8_t spi_txrx(uint8_t b)         /* 全双工：发一字节，同时收回一字节 */
{
    while (!(SPI1->SR & SPI_SR_TXE)) { }   /* 等发送缓冲空 */
    SPI1->DR = b;
    while (!(SPI1->SR & SPI_SR_RXNE)) { }  /* 等收到（移位完成） */
    return (uint8_t)SPI1->DR;
}

/* ============ W25Q16 协议层 ============ */

static void flash_write_enable(void)
{
    CS_L();
    spi_txrx(0x06);
    CS_H();
}

static uint8_t flash_wait_busy(void)       /* 轮询状态寄存器 bit0（擦写中=1），带超时 */
{
    uint32_t t = 1000000;
    uint8_t sr;
    CS_L();
    spi_txrx(0x05);
    do {
        sr = spi_txrx(0xFF);
    } while ((sr & 0x01) && --t);
    CS_H();
    return t ? 1 : 0;
}

static uint32_t flash_read_jedec(void)
{
    uint32_t id;
    CS_L();
    spi_txrx(0x9F);
    id  = (uint32_t)spi_txrx(0xFF) << 16;
    id |= (uint32_t)spi_txrx(0xFF) << 8;
    id |= (uint32_t)spi_txrx(0xFF);
    CS_H();
    return id;
}

static uint8_t flash_erase_sector(void)    /* 擦 0 地址所在 4KB 扇区 */
{
    flash_write_enable();
    CS_L();
    spi_txrx(0x20);
    spi_txrx(0x00); spi_txrx(0x00); spi_txrx(0x00);   /* 24 位地址 */
    CS_H();
    return flash_wait_busy();              /* 典型 45~300ms */
}

static uint8_t flash_prog_page(const uint8_t *d, uint8_t len)   /* 页编程（≤256 字节） */
{
    uint8_t i;
    flash_write_enable();
    CS_L();
    spi_txrx(0x02);
    spi_txrx(0x00); spi_txrx(0x00); spi_txrx(0x00);
    for (i = 0; i < len; i++)
        spi_txrx(d[i]);
    CS_H();
    return flash_wait_busy();              /* 典型 0.7~3ms */
}

static void flash_read_data(uint8_t *out, uint8_t len)
{
    uint8_t i;
    CS_L();
    spi_txrx(0x03);
    spi_txrx(0x00); spi_txrx(0x00); spi_txrx(0x00);
    for (i = 0; i < len; i++)
        out[i] = spi_txrx(0xFF);           /* 读：发哑字节造时钟 */
    CS_H();
}

/* ============ 主线 ============ */

int main(void)
{
    uint32_t id;
    uint8_t rd[4] = {0};
    const uint8_t wr[4] = {'F', '4', '0', '7'};
    uint8_t i, ok = 1;

    uart_init();
    spi1_init();

    uart1_str("\r\n=== SPI1 W25Q16 TEST (reg) ===\r\n");

    id = flash_read_jedec();
    uart1_str("JEDEC=0x");
    uart1_hex((uint8_t)(id >> 16));
    uart1_hex((uint8_t)(id >> 8));
    uart1_hex((uint8_t)id);
    if (id == 0xEF4015UL) uart1_str("  ID-PASS\r\n");
    else                  uart1_str("  ID-FAIL\r\n");

    ok &= flash_erase_sector();
    ok &= flash_prog_page(wr, 4);
    flash_read_data(rd, 4);

    uart1_str("READ=");
    for (i = 0; i < 4; i++)
        uart1_send(rd[i]);
    uart1_str("  ");
    for (i = 0; i < 4; i++)
        if (rd[i] != wr[i]) ok = 0;
    if (ok) uart1_str("PASS\r\n");
    else    uart1_str("FAIL\r\n");

    while (1) { }
}
