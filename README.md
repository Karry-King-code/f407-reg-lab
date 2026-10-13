# f407-reg-lab · STM32F407VET6 外设驱动（寄存器版）

导师任务：用开发板 + AI 调通所有外设，**寄存器与 HAL 各一份实现，先做寄存器**。
本仓库 = 寄存器腿。HAL 腿见 [f407-hal-lab](https://github.com/Karry-King-code/f407-hal-lab)。

## 硬件平台

STM32F407 主控板 V1.0（嘉立创 EDA，原理图 7 页，导师提供）：
- MCU：STM32F407VET6（LQFP100，512KB Flash，192KB SRAM）
- HSE 晶振：8MHz（原理图 X1）；LSE：32.768kHz
- 调试口：SWD（SWCLK/SWDIO/GND/3V3/BOOT0/BOOT1 排针）
- 板载外设：3 LED、KEY1-4、蜂鸣器、继电器、CH340N 串口、W25Q16 Flash、
  AT24C02 EEPROM、2.4寸 ST7789 触摸屏、ICM-42688 IMU、AHT20、ES8388 音频、
  SD 卡、EC11 编码器、CAN、RS485、RS232、ESP8266-01S

## 节点进度（每节点 = 编译0错0警 + 烧录 + 现象验证 + 文档 + 独立提交）

| 节点 | 内容 | 状态 |
|---|---|---|
| 1 | 环境搭建 + LED 点灯（PD10，高电平亮） | 代码就绪待上板 |
| 2 | 按键（KEY1-3=PD13-15 低有效） | - |
| 3 | 串口（USART1 PA9/PA10 经 CH340N） | - |
| 4 | I2C（AT24C02=硬件I2C2 PB10/11；AHT20=软件I2C PE2/3） | - |
| 5 | SPI（W25Q16=硬件SPI1 PA4-7） | - |
| 6 | 屏幕（ST7789 SPI：SCK=PB3/MOSI=PB5/DC=PB0/CS=PA15/RST=PC7/LED=PB1） | - |
| 7 | LVGL 移植 | - |
| 8 | ADC | - |
| 9 | DAC | - |

完整引脚分配表见 `docs/硬件事实表.md`（来源：原理图第 7 页坐标提取 + 视觉读图双源核实）。

## 构建

```
cd MDK-ARM
C:/Keil_v5/UV4/UV4.exe -r f407-reg-lab.uvprojx -j0 -o build.log
```

烧录（板到手后）：
```
C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe -c port=SWD mode=UR -w Objects/f407-reg-lab.hex -v -rst
```
（命令必须以 -rst 结尾，否则芯片停在暂停态表现为"全无反应"——F103 项目血泪教训）
