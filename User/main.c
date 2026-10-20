/*
 * 节点 7 · LVGL 移植（寄存器版）
 *
 * 硬件：ST7789 240x320（同节点六：SCK=PB3 MOSI=PB5 DC=PB0 CS=PA15 RST=PC7 LED=PB1）
 * LVGL：v8.3.11，16 位色，大端（LV_COLOR_16_SWAP=1）
 *
 * 三个移植要点：
 *   ① flush 回调：LVGL 画好一块后叫你把像素刷到屏上
 *   ② tick：LVGL 的"心跳"，主循环里 lv_tick_inc(5) 喂
 *   ③ 主循环必须调 lv_timer_handler()（LVGL 的"大脑"，不调就永远不动）
 */

#include "stm32f4xx.h"
#include "lvgl.h"

/* ============ ST7789（节点六驱动，精简版） ============ */
#define CS_L()    (GPIOA->BSRR = (1UL << 15) << 16)
#define CS_H()    (GPIOA->BSRR = (1UL << 15))
#define DC_L()    (GPIOB->BSRR = (1UL << 0) << 16)
#define DC_H()    (GPIOB->BSRR = (1UL << 0))
#define RST_L()   (GPIOC->BSRR = (1UL << 7) << 16)
#define RST_H()   (GPIOC->BSRR = (1UL << 7))
#define LED_ON()  (GPIOB->BSRR = (1UL << 1))

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

static void spi_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    GPIOB->MODER &= ~((3UL << 6) | (3UL << 8) | (3UL << 10));
    GPIOB->MODER |=  (2UL << 6) | (2UL << 8) | (2UL << 10);
    GPIOB->AFR[0] &= ~((0xFUL << 12) | (0xFUL << 16) | (0xFUL << 20));
    GPIOB->AFR[0] |=  (5UL << 12) | (5UL << 16) | (5UL << 20);

    GPIOB->MODER &= ~((3UL << 0) | (3UL << 2));
    GPIOB->MODER |=  (1UL << 0) | (1UL << 2);
    GPIOA->MODER &= ~(3UL << 30);  GPIOA->MODER |= (1UL << 30);
    GPIOC->MODER &= ~(3UL << 14);  GPIOC->MODER |= (1UL << 14);

    CS_H(); DC_H();

    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI1->CR1 |= SPI_CR1_SPE;
}

static uint8_t spi_tx(uint8_t b)
{
    while (!(SPI1->SR & SPI_SR_TXE)) { }
    SPI1->DR = b;
    while (!(SPI1->SR & SPI_SR_RXNE)) { }
    return (uint8_t)SPI1->DR;
}

static void lcd_cmd(uint8_t c) { CS_L(); DC_L(); spi_tx(c); CS_H(); }
static void lcd_data(uint8_t d) { CS_L(); DC_H(); spi_tx(d); CS_H(); }

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_cmd(0x2A);
    lcd_data((uint8_t)(x0 >> 8)); lcd_data((uint8_t)(x0 & 0xFF));
    lcd_data((uint8_t)(x1 >> 8)); lcd_data((uint8_t)(x1 & 0xFF));
    lcd_cmd(0x2B);
    lcd_data((uint8_t)(y0 >> 8)); lcd_data((uint8_t)(y0 & 0xFF));
    lcd_data((uint8_t)(y1 >> 8)); lcd_data((uint8_t)(y1 & 0xFF));
    lcd_cmd(0x2C);
}

static void lcd_init(void)
{
    RST_H(); delay_ms(10);
    RST_L(); delay_ms(20);
    RST_H(); delay_ms(120);

    lcd_cmd(0x01); delay_ms(150);
    lcd_cmd(0x11); delay_ms(120);
    lcd_cmd(0x3A); lcd_data(0x55);
    lcd_cmd(0x36); lcd_data(0x00);
    lcd_cmd(0x20);                  /* 反色关（节点六实测） */
    lcd_cmd(0x13);
    lcd_cmd(0x29); delay_ms(20);
}

/* ============ LVGL 移植 ============ */

#define LCD_W 240
#define LCD_H 320

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[LCD_W * 20];      /* 20 行缓冲：240*20*2 = 9.6KB */
static lv_disp_drv_t disp_drv;

/* ① flush：LVGL 每画好一块就调这里，把这块像素刷到屏幕 */
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    uint32_t i, n = w * h;

    lcd_set_window((uint16_t)area->x1, (uint16_t)area->y1,
                   (uint16_t)area->x2, (uint16_t)area->y2);
    CS_L(); DC_H();
    for (i = 0; i < n; i++)
    {
        uint16_t c = color_p[i].full;
        spi_tx((uint8_t)(c >> 8));       /* 大端：高字节先 */
        spi_tx((uint8_t)(c & 0xFF));
    }
    CS_H();

    lv_disp_flush_ready(drv);            /* 告诉 LVGL：这块刷完了 */
}

/* 建一个简单界面：深色背景 + 标题 + 进度条 */
static void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x102040), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "F407 + LVGL 8.3");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "ST7789 240x320");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x80C0FF), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 55);

    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 180, 18);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_value(bar, 65, LV_ANIM_OFF);
}

int main(void)
{
    spi_init();
    LED_ON();
    lcd_init();

    lv_init();                           /* LVGL 内核启动 */
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, LCD_W * 20);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = LCD_W;
    disp_drv.ver_res  = LCD_H;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);     /* 注册显示设备 */

    ui_create();

    while (1)
    {
        lv_tick_inc(5);                  /* ② 喂心跳（5ms） */
        lv_timer_handler();              /* ③ 跑 LVGL 的活 */
        delay_ms(5);
    }
}
