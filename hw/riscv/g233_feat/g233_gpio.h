#ifndef G233_GPIO_H
#define G233_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_G233_GPIO "g233-gpio"
typedef struct G233GPIOState G233GPIOState;
DECLARE_INSTANCE_CHECKER(G233GPIOState, G233_GPIO,
                         TYPE_G233_GPIO)

#define G233_GPIO_DIR   0x00
#define G233_GPIO_OUT   0x04
#define G233_GPIO_IN    0x08
#define G233_GPIO_IE    0x0C
#define G233_GPIO_IS    0x10
#define G233_GPIO_TRIG  0x14
#define G233_GPIO_POL   0x18


#define G233_GPIO_SIZE 0x100


struct G233GPIOState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t dir;   /* gpio 方向 */
    uint32_t out;   /* gpio 输出 */
    uint32_t in;    /* gpio 输入 */
    uint32_t ie;    /* gpio 中断使能 */
    uint32_t is;    /* gpio 中断状态，写 1 清除 */
    uint32_t trig;  /* gpio 中断触发：0=edge，1=level */
    uint32_t pol;   /* gpio 中断极性：0=low/falling，1=high/rising */
    uint32_t prev_in;

    /*
     * Edge 状态是 sticky 的，直到 guest 写 GPIO_IS 清除；level 状态由
     * 当前 pin 电平实时重算。二者分开保存，避免 TRIG/POL 切换时把
     * 旧 level 状态误当成 edge sticky 状态。
     */
    uint32_t edge_is;
    uint32_t level_is;
};

#endif
