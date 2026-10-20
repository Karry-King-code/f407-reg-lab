/*
 * 节点 6 · 屏幕 ST7789（寄存器版）
 *
 * 硬件事实（原理图第 4/7 页已核实）：
 *   U9 = 2.4 寸 ST7789 触摸屏（14 针座，已插好）
 *   SCK=PB3  MOSI=PB5  （SPI1 的 PB 组映射，AF5；PA4-7 那组被 Flash 占用）
 *   DC=PB0  CS=PA15  RST=PC7  LED=PB1（背光）
 *   MISO=PB4（屏幕不返回数据，不用）
 *
 * ★ PB3/PB4/PA15 复位后是 JTAG 引脚（JTDO/NJTRST/JTDI）。我们用 SWD 调试，
 *   所以直接把它们配成 AF5/GPIO 输出即可，不影响 ST-Link。
 *
 * 功能：初始化屏幕 → 红/绿/蓝全屏循环，每种颜色停 1 秒
 */

#include "stm32f4xx.h"

/* ============ 引脚操作宏 ============ */
#define CS_L()    (GPIOA->BSRR = (1UL << 15) << 16)
#define CS_H()    (GPIOA->BSRR = (1UL << 15))
#define DC_L()    (GPIOB->BSRR = (1UL << 0) << 16)
#define DC_H()    (GPIOB->BSRR = (1UL << 0))
#define RST_L()   (GPIOC->BSRR = (1UL << 7) << 16)
#define RST_H()   (GPIOC->BSRR = (1UL << 7))
#define LED_ON()  (GPIOB->BSRR = (1UL << 1))
#define LED_OFF() (GPIOB->BSRR = (1UL << 1) << 16)

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

/* ============ SPI1（PB3/PB5 组） ============ */
static void spi_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /* PB3=SCK, PB5=MOSI, PB4=MISO：复用 10 + AF5 */
    GPIOB->MODER &= ~((3UL << 6) | (3UL << 8) | (3UL << 10));
    GPIOB->MODER |=  (2UL << 6) | (2UL << 8) | (2UL << 10);
    GPIOB->AFR[0] &= ~((0xFUL << 12) | (0xFUL << 16) | (0xFUL << 20));
    GPIOB->AFR[0] |=  (5UL << 12) | (5UL << 16) | (5UL << 20);   /* PB3/PB4/PB5 = AF5 */

    /* PB0=DC, PB1=LED：普通输出 */
    GPIOB->MODER &= ~((3UL << 0) | (3UL << 2));
    GPIOB->MODER |=  (1UL << 0) | (1UL << 2);

    /* PA15=CS：普通输出（JTDI 脚，配成输出即释放） */
    GPIOA->MODER &= ~(3UL << 30);
    GPIOA->MODER |=  (1UL << 30);

    /* PC7=RST：普通输出 */
    GPIOC->MODER &= ~(3UL << 14);
    GPIOC->MODER |=  (1UL << 14);

    CS_H();
    DC_H();

    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;   /* BR=000: 8MHz */
    SPI1->CR1 |= SPI_CR1_SPE;
}

static uint8_t spi_tx(uint8_t b)
{
    while (!(SPI1->SR & SPI_SR_TXE)) { }
    SPI1->DR = b;
    while (!(SPI1->SR & SPI_SR_RXNE)) { }
    return (uint8_t)SPI1->DR;
}

/* ============ ST7789 ============ */
static void lcd_cmd(uint8_t c)
{
    CS_L(); DC_L(); spi_tx(c); CS_H();
}

static void lcd_data(uint8_t d)
{
    CS_L(); DC_H(); spi_tx(d); CS_H();
}

static void lcd_data_start(void)
{
    CS_L(); DC_H();
}

static void lcd_data_end(void)
{
    CS_H();
}

static void lcd_reset(void)
{
    RST_H(); delay_ms(10);
    RST_L(); delay_ms(20);
    RST_H(); delay_ms(120);
}

static void lcd_init(void)
{
    lcd_reset();

    lcd_cmd(0x01); delay_ms(150);          /* 软复位 */
    lcd_cmd(0x11); delay_ms(120);          /* 退出睡眠 */

    lcd_cmd(0x3A); lcd_data(0x55);         /* 16 位色（RGB565） */
    lcd_cmd(0x36); lcd_data(0x00);         /* 扫描方向：正常 */
    lcd_cmd(0x20);                         /* 反色关：实测本屏开了 0x21 会红绿蓝全反色 */
    lcd_cmd(0x13);                         /* 正常显示模式 */
    lcd_cmd(0x29); delay_ms(20);           /* 显示开 */
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_cmd(0x2A);                          /* 列地址 */
    lcd_data(x0 >> 8); lcd_data(x0 & 0xFF);
    lcd_data(x1 >> 8); lcd_data(x1 & 0xFF);
    lcd_cmd(0x2B);                          /* 行地址 */
    lcd_data(y0 >> 8); lcd_data(y0 & 0xFF);
    lcd_data(y1 >> 8); lcd_data(y1 & 0xFF);
    lcd_cmd(0x2C);                          /* 开始写显存 */
}

static void lcd_fill(uint16_t color)        /* 全屏填充 240x320 */
{
    uint32_t i, n = 240UL * 320UL;
    lcd_set_window(0, 0, 239, 319);
    lcd_data_start();
    for (i = 0; i < n; i++)
    {
        spi_tx((uint8_t)(color >> 8));      /* RGB565 高字节先 */
        spi_tx((uint8_t)(color & 0xFF));
    }
    lcd_data_end();
}

int main(void)
{
    spi_init();
    LED_ON();                               /* 背光开，否则黑屏看不出 */
    lcd_init();

    while (1)
    {
        lcd_fill(0xF800);                   /* 红 */
        delay_ms(1000);
        lcd_fill(0x07E0);                   /* 绿 */
        delay_ms(1000);
        lcd_fill(0x001F);                   /* 蓝 */
        delay_ms(1000);
    }
}
