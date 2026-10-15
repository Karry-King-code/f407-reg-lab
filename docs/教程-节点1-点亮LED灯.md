# 节点一 · 点亮 LED 灯

> **分工**：资料/截图/讲解 AI 备好，代码你手敲（practice 分支），卡壳 15 分钟再看 main 分支参考答案。
> **两版关系**：寄存器版和 HAL 版思路一样（找灯 → 开时钟 → 配输出 → 亮灭交替），共用的放前面，差异各自章节。
> **流程**：每版都要 编译 0 错 0 警 → 烧录 → 亲眼看到闪 → git 提交（`1. xxx` 开头）。

---

## 0. 接线

| 接什么 | 怎么接 |
|---|---|
| DC 电源（必须） | 电源座插适配器（7~24V），**ON 开关拨到 ON** |
| ST-Link | `SWDIO→SWD`、`SWCLK→SCK`、`GND→GND` 三根（右缘 5 针排针，3V3/RST 不用接） |
| USB-C | 插 **USB>TTL** 口（串口日志用；旁边的 OTG 口插不插都行） |

**自检两条**：
1. 通电标志：电脑设备管理器出现 CH340 串口号（CH340 吃板载 3V3，板不通电它不出现）。
2. ST-Link 通不通：
```bash
"C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe" -c port=SWD mode=UR
```
   看到 `Device ID : 0x413`（= F407 家族）+ `Device name : STM32F405xx/F407xx...` 就是全通。

---

## 1. 看图找灯

![LED1 电路](images/sch_p3_led1.png)

**怎么读**：原理图第 3 页。电流方向：PD10 → LED → 4.7K → GND，所以 **PD10 输出高电平灯亮**。
⚠️ 与 F103 板相反（那块板低电平亮），一切以原理图为准。位号 LED3 ≠ 网络名 LED1，别混。

![引脚分配](images/sch_p7_led_pin.png)

**怎么读**：第 7 页引脚表：**LED1 = PD10**（红灯）。同列：LED2 = PD11（绿）、LED3 = PD12（蓝）、KEY1~3 = PD13~15（节点二用）。

---

## 2. 原理

**四步流程**（F103 学的方法不变）：确定引脚 → 开时钟 → 配输出 → 亮灭交替。
**变的是寄存器**（F1 → F4 迁移表）：

| 步骤 | F103 | F407 |
|---|---|---|
| 开 GPIO 时钟 | `RCC->APB2ENR` | `RCC->AHB1ENR`（换域了） |
| 配输入/输出 | CRL/CRH 每脚 4 位 | **CRL/CRH 没了**，用 MODER/OTYPER/OSPEEDR/PUPDR 四个寄存器 |
| 输出电平 | BSRR / BRR / ODR | **只有 BSRR**（BRR 没了，实测踩坑见第 6 节） |

### 2.1 开时钟

![RCC_AHB1ENR](images/rm_p244_ahb1enr.png)

**怎么读**：RM0090 中文版第 244 页，`RCC_AHB1ENR` 位表——**bit3 = GPIODEN**，写 1 给 GPIOD 供电。
手册依据：第 243 页（表头、偏移 0x30）+ 第 244 页（位表）。

### 2.2 配输出

![GPIOx_MODER](images/rm_p283_moder.png)

**怎么读**：第 283 页 `GPIOx_MODER`——**每脚 2 位**：00 输入 / 01 输出 / 10 复用 / 11 模拟。
PD10 占 **bit[21:20]**（脚号 × 2 = 起始位；21 是包的高位格，20 是低位格）。
`1<<20` 把 01 写进包；`1<<21` 会写成 10（复用）——错一位就是另一种模式，编译器不报错。
复位默认全 0 = 输入模式（**只会听不会说，忘了配这步灯永远不亮**——我们踩过，见第 6 节）。

### 2.3 亮与灭

![GPIOx_BSRR](images/rm_p286_bsrr.png)

**怎么读**：第 286 页 `GPIOx_BSRR`——低 16 位 BSx 写 1 = 输出高；高 16 位 BRx 写 1 = 输出低；写 0 无效。
本板：`BS10`（bit10）= 亮，`BR10`（bit26）= 灭。一次写完、不读旧值 = **原子操作**（中断插不进来）。
`|=` 是读-改-写（三条指令），初始化时用没风险，主循环反复翻电平用 BSRR。
手册依据：第 286 页。

### 2.4 延时

![时钟树](images/rm_p217_clocktree.png)

**怎么读**：第 217 页时钟树，复位默认走 **HSI 16MHz**（第 219 页 §7.2.2 原话"internal 16 MHz RC oscillator"）。
1ms = 16000 个时钟（直连 HCLK）或 2000 个（HCLK÷8，CTRL 不置 bit2）。**两种都行**，参数要跟选的路配套。
时钟先不配 PLL——等时钟节点再上 168MHz（届时必须实证晶振 8MHz、显式 `HSE_VALUE=8000000`）。

---

## 3. 寄存器版

**要碰的寄存器**：

| 寄存器 | 地址 | 干什么 |
|---|---|---|
| `RCC->AHB1ENR` | 0x40023830 | `|= 1<<3` 开 GPIOD 时钟 |
| `GPIOD->MODER` | 0x40020C00 | 先 `&= ~(3<<20)` 再 `\|= (1<<20)` 配输出 |
| `GPIOD->BSRR` | 0x40020C18 | 亮 `1<<10`，灭 `1<<26` |

**核心代码**（practice 分支手敲；`delay_ms` 用 SysTick，原理同 F103，LOAD 填 16000−1 或 2000−1 看你选哪条路）：

```c
int main(void)
{
    SystemCoreClockUpdate();                    /* 读当前频率到 SystemCoreClock */

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;        /* 开 GPIOD 时钟 */

    GPIOD->MODER &= ~(3UL << 20);               /* 清 [21:20] 旧值 */
    GPIOD->MODER |=  (1UL << 20);               /* [21:20]=01 输出 */

    GPIOD->BSRR = GPIO_BSRR_BR10;               /* 上电先灭 */

    while (1)
    {
        GPIOD->BSRR = GPIO_BSRR_BR10;           /* 灭 */
        delay_ms(500);
        GPIOD->BSRR = GPIO_BSRR_BS10;           /* 亮 */
        delay_ms(500);
    }
}
```

**扩展挑战**（你自己加的，双灯）：PD11 同公式——`3<<22` 清、`1<<22` 写、亮 `BS11` 灭 `BR11`。

**验收**：0 错 0 警（Code≈700~800B）→ 红灯 500ms 闪、上电先灭。

---

## 4. HAL 版

**HAL 只是个代办员**，对照表：

| 你写的 | 它替你写的寄存器 |
|---|---|
| `__HAL_RCC_GPIOD_CLK_ENABLE()` | `AHB1ENR \|= 1<<3` |
| `HAL_GPIO_Init(GPIOD, &...)` | MODER/OTYPER/OSPEEDR/PUPDR 四连写 |
| `HAL_GPIO_WritePin(...SET/RESET)` | BSRR 置位 / 清零 |
| `HAL_Delay(500)` | SysTick 1ms 中断累计 uwTick 查差值 |

**核心代码**（脚手架 `HAL_Init` 和 SysTick 心跳已在工程里）：

```c
int main(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  HAL_Init();                     /* SysTick 1ms 心跳 */

  __HAL_RCC_GPIOD_CLK_ENABLE();   /* 开 GPIOD 时钟 */

  GPIO_InitStruct.Pin   = GPIO_PIN_10;          /* PD10 */
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;  /* 推挽输出 */
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);       /* 四连写 */

  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_RESET);   /* 上电先灭 */

  while (1)
  {
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_RESET);  /* 灭 */
    HAL_Delay(500);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_SET);    /* 亮 */
    HAL_Delay(500);
  }
}
```

**时钟不用写**：`SystemClock_Config` 以后节点再说。F103 板那份 `HSI/2×16=64MHz` 配置在 F4 上编译不过——
`RCC_HSE_PREDIV_DIV1`、`RCC_PLL_MUL16` 是 F1 专属宏，直接删。

**验收**：0 错 0 警（Code≈3.2KB）→ 现象与寄存器版一致。

---

## 5. 编译烧录

**铁律四步**：改代码 → **Ctrl+S** → **F7**（重新编译）→ **LOAD**（下载）。
LOAD **不会**自动编译，不 F7 烧的是旧固件（我们用 700B vs 728B 的体积差抓过现行）。

**首次用 Keil 烧录，先配一次**（魔术棒 = Options for Target，ALT+F7）：
1. **Debug** 页：下拉框选 **ST-Link Debugger**（默认 ULINK 会报 No ULINK Device）→ Settings → 看到 ST-Link 序列号和 IDCODE 即通，Port = SW
2. **Flash Download** 页：算法列表有 **STM32F4xx Flash 512KB**（没有点 Add 加）；**勾选 "Reset and Run"** ←★
3. 确定。以后 LOAD 一键 = 烧录 + 复位 + 开跑

命令行等价（Git Bash，`-rst` = Reset and Run）：
```bash
cd C:/Users/26476/Desktop/F407-Lab/reg/f407-reg-lab/MDK-ARM
"C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe" -c port=SWD mode=UR -w Objects/f407-reg-lab.hex -v -rst
```

**看到现象后提交**：
```bash
cd C:/Users/26476/Desktop/F407-Lab/reg/f407-reg-lab
git add -A && git commit -m "1. 点亮LED灯（寄存器版）" && git push
```

---

## 6. 翻车实录

| 现象 | 真因 | 解法 |
|---|---|---|
| 编译 0 错，灯不亮 | 代码没配 MODER，PD10 还是输入模式（寄存器读数：AHB1ENR bit3=1 但 MODER=0，钉死） | 补“先清后写”两行；学会读寄存器破案 |
| 改了代码，烧完还是老现象 | LOAD 不自动编译，烧进的是旧 hex（体积 700B vs 728B 暴露） | **F7 之后再 LOAD** |
| 烧录成功，灯全灭 | 烧完芯片停在暂停态（Keil 没勾 Reset and Run / 命令行没 -rst） | 勾上它 / 命令带 -rst |
| 编译报 `has no field "BRR"` | F103 的 BRR 寄存器 F4 上不存在 | 清零用 `GPIO_BSRR_BR10`（BSRR 高 16 位） |
| HAL 报 `RCC_HSE_PREDIV_DIV1` 未定义 | F1 专属宏，F4 没有 | 删 SystemClock_Config（本节点不需要时钟配置） |
| 绿灯很暗 | 绿光 LED 压降 ≈3.0V（红光 ≈2.0V），同 4.7K 电阻电流只有红灯 1/4 | 正常物理现象，不是 bug |
| 灯接 PD10 写成 PC13/GPIOC | F103 板的 LED 在 PC13，肌肉记忆 | 一切以本板原理图为准 |

---

## 7. 验收

| 腿 | 编译 | 现象 | 体积 | 提交 |
|---|---|---|---|---|
| 寄存器 | ✅ 0错0警 | ✅ 红灯500ms闪+自扩展双灯 | 784B | `1. 点亮LED灯（寄存器版）` |
| HAL | ✅ 0错0警 | ✅ 红灯500ms闪 | 3188B | `1. 点亮LED灯（HAL库版）` |

**下一步**：节点二 · 按键（KEY1~3 = PD13~15，低电平有效）。
