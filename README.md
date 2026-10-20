# f407-reg-lab · STM32F407VET6 外设驱动（寄存器版）

导师任务：用开发板 + AI 调通所有外设，**寄存器与 HAL 各一份实现，先做寄存器**。
本仓库 = 寄存器腿。HAL 腿见 [f407-hal-lab](https://github.com/Karry-King-code/f407-hal-lab)。

## 硬件平台

STM32F407 主控板 V1.0（ZeeRich 智启芯电子，原理图 7 页）：
- MCU：STM32F407VET6（LQFP100，512KB Flash，192KB SRAM）
- HSE 晶振：8MHz（原理图 X1）；复位默认跑 HSI 16MHz（本项目所有节点都用 HSI，尚未上 PLL）
- 调试：ST-Link V2 / SWD（右缘 5 针：3V3 GND SWD SCK RST）
- 供电：**DC 电源座 + ON 开关**（两个 USB-C 口是纯数据口，不供电）
- 板载：3 LED、4 按键、蜂鸣器、继电器、CH340N 串口、W25Q16 Flash、
  AT24C02 EEPROM、2.4 寸 ST7789 触摸屏、SD 卡、CAN/RS485/RS232、ESP8266 兼容座

## 节点进度（每节点 = 编译 0 错 + 烧录 + 现象验证 + 文档 + 独立提交）

| 节点 | 内容 | 状态 |
|---|---|---|
| 1 | 点亮 LED 灯（PD10 高电平亮，含双灯扩展） | ✅ 784B 上板 |
| 2 | 按键控制 LED（KEY1=PD13 低有效，按键开关闪烁） | ✅ 848B 上板 |
| 3 | 串口收发（USART1 PA9/PA10，echo + 中断环形缓冲） | ✅ 1744B 上板，S:81=R:81 |
| 4 | I2C 读写 EEPROM（AT24C02，硬件 I2C2 + 位带两实现） | ✅ 1976B / 1864B，RD=0x5A PASS |
| 5 | SPI 读写 Flash（W25Q16，JEDEC + 擦写读） | ✅ 1744B，JEDEC=0xEF4015 |
| 6 | 屏幕 ST7789（SPI，RGB565，全屏填色） | ✅ 1316B，红绿蓝循环 |
| 7 | LVGL 移植（v8.3.11 + ST7789 显示驱动） | ✅ 227KB，标题+进度条 |
| 8 | ADC 内部温度传感器（IN16/IN17，VREF 校验） | ✅ 寄存器1324B / HAL 10656B |
| 9 | DAC 输出 + ADC 回读自测（PA4，与 Flash CS 冲突已说明） | ✅ 寄存器1332B / HAL 13248B |

## 文档

**主教材**：[`docs/F407外设通关教程.md`](docs/F407外设通关教程.md)

内容结构：
- **资料获取方法论**（开头）：五类资料各管什么、每个节点真实查了哪一页
- **开始之前**：接线 + 烧录（Keil 魔术棒配置、Reset and Run 大坑）
- **节点一 ~ 七**：每章 = 看图说话 → 原理（手册截图+怎么读）→ 寄存器版完整代码 → HAL 版完整代码 → 验收 → 翻车实录

**图片**：`docs/images/`（原理图裁剪 + RM0090/PM0214 手册截图，均带红框标注）

## 每个节点的代码

`User/chapters/` 下每章一个独立文件（复制进 `User/main.c` 即可编译）：

| 文件 | 对应 |
|---|---|
| `ch1_led.c` | 节点一 点灯（双灯版） |
| `ch2_key.c` | 节点二 按键开关闪烁 |
| `ch3_uart.c` | 节点三 串口（中断 + 环形缓冲） |
| `ch4_i2c_hw.c` | 节点四 硬件 I2C2 读写 EEPROM（主线） |
| `ch4_i2c_bitbang.c` | 节点四 位带 I2C（协议原理课） |
| `ch5_spi.c` | 节点五 SPI 读写 W25Q16 |
| `ch6_lcd.c` | 节点六 ST7789 显示 |
| `ch7_lvgl.c` | 节点七 LVGL 移植 |

## 构建与烧录

**编译**（Keil UV4 命令行，或 Keil 里按 F7）：
```bash
cd MDK-ARM
C:/Keil_v5/UV4/UV4.exe -r f407-reg-lab.uvprojx -j0 -o build.log
```

**烧录**（命令必带 `-rst`，否则芯片停在暂停态表现为"全无反应"）：
```bash
"C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe" \
  -c port=SWD mode=UR -w Objects/f407-reg-lab.hex -v -rst
```

**Keil 里烧录**需要先配一次（魔术棒 ALT+F7）：
- Debug 页 → ST-Link Debugger → Settings（看到序列号即通）
- Flash Download 页 → 算法 `STM32F4xx Flash 512KB` + 勾 **Reset and Run**

## 目录结构

```
f407-reg-lab/
├── Libraries/CMSIS/         CMSIS 内核 + STM32F4 设备头文件 + 启动文件
├── Middlewares/
│   ├── lvgl/                LVGL v8.3.11 源码（节点七）
│   └── lv_conf.h            LVGL 配置（必须在 lvgl 同级）
├── User/
│   ├── main.c               当前节点代码（现在 = 节点七 LVGL）
│   └── chapters/            各节点独立代码存档
├── MDK-ARM/                 Keil 工程
└── docs/                    教程 + 截图
```

## 踩过的坑（都在文档翻车实录里）

1. F4 没有 `BRR` 寄存器（F103 有）——清零只能走 BSRR 高位
2. `AFR` 在代码里是 `AFR[2]` 数组（手册叫 `GPIOx_AFRH`）
3. `CS_L()/CS_H()` 宏 BSRR 高低位写反 → 芯片永远不被选中，读回 `0x000000`
4. I2C 引脚必须**开漏**，推挽会假 NAK
5. AT24C02 写后有 5ms 编程周期，期间全 NAK——必须 ACK 轮询
6. Keil LOAD **不自动编译**，改代码必须先 F7
7. 烧录命令不带 `-rst` → 灯不闪但代码没问题
8. ST7789 反色（0x21）会让红绿蓝全反——本屏必须 0x20
9. `lv_conf.h` 必须在 lvgl 同级目录，且用官方模板做底（自己精简会触发依赖链）
