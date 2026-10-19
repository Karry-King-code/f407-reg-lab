/*
 * 节点 4 · I2C 读写 EEPROM（AT24C02）（练习版 · 空白模板）
 *
 * 硬件事实（原理图第 4/7 页已核实）：
 *   AT24C02 EEPROM：SCL = PB10、SDA = PB11 —— 正好是硬件 I2C2 的引脚（AF4）
 *   器件地址：0xA0（写）/ 0xA1（读），容量 256 字节，掉电不丢
 *   （I2C 是总线：SCL 由主机打拍子，SDA 双向传数据；两根线都靠上拉，空闲为高）
 *
 * 目标现象（版本 A）：
 *   向 EEPROM 某地址写一个字节 → 读回来 → 两值相等 = 读写闭环
 *   结果可从串口看（把节点三的串口初始化和发送函数搬进来一起用）
 *
 * 步骤骨架（F407 的 I2C 是老式外设：CR1/CR2/DR/SR1/SR2/CCR）：
 *   ① 开时钟：GPIOB = AHB1ENR bit1，I2C2 = APB1ENR bit22
 *   ② PB10/PB11 复用 AF4：MODER [21:20]=10、[23:22]=10；AFR[1] [11:8]=0100、[15:12]=0100
 *   ③ I2C2->CR2 = 16        （告诉 I2C：输入时钟是 16MHz，用于内部时序）
 *      I2C2->CCR = 80       （100kHz 标准模式：16M ÷ (2×100k) = 80）
 *      I2C2->TRISE = 17     （最大上升时间 1000ns，换算成 16MHz 周期数 +1）
 *      I2C2->CR1 |= PE      （外设总开关）
 *   ④ 写流程：START位 → 等 SB → 发 0xA0 → 等 ADDR → 读 SR1+SR2 清 ADDR →
 *             发数据字节 → 等 BTF → 发 STOP
 *   ⑤ 读流程：同上发到目标地址后 → 重新 START → 发 0xA1 → 等 ADDR → 清 →
 *             收字节（最后一个发 NACK）→ STOP
 *
 * 新概念：I2C 总线协议（起始/地址/应答/停止）、多从机共享两根线
 * 教材：总文档"节点四"章（下一步写好；先按本模板骨架敲，卡壳问我）
 */

#include "stm32f4xx.h"

int main(void)
{
    SystemCoreClockUpdate();

    /* ① 开时钟：GPIOB（AHB1 bit1）+ I2C2（APB1ENR bit22） */

    /* ② PB10/PB11 复用 AF4：MODER + AFR[1] */

    /* ③ I2C2 时序三件套：CR2=16、CCR=80、TRISE=17，最后 CR1 |= PE */

    /* ④⑤ 写一个字节再读回来（流程见上），把结果存到变量（或用节点三的串口打出来） */

    while (1)
    {
    }
}
