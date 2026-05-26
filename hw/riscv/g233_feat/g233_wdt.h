#ifndef G233_WDT_H
#define G233_WDT_H

#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_G233_WDT "g233-wdt"
typedef struct G233WDTState G233WDTState;
DECLARE_INSTANCE_CHECKER(G233WDTState, G233_WDT,
                         TYPE_G233_WDT)

#define G233_WDT_SIZE 0x100
#define G233_WDT_FREQ_HZ 1000

#define G233_WDT_CTRL 0x00
#define G233_WDT_LOAD 0x04
#define G233_WDT_VAL  0x08
#define G233_WDT_KEY  0x0c
#define G233_WDT_SR   0x10

#define G233_WDT_CTRL_EN    BIT(0)
#define G233_WDT_CTRL_INTEN BIT(1)

#define G233_WDT_KEY_FEED 0x5a5a5a5a
#define G233_WDT_KEY_LOCK 0x1acce551

#define G233_WDT_SR_TIMEOUT BIT(0)

struct G233WDTState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    ptimer_state *timer;

    uint32_t ctrl;
    uint32_t load;
    uint32_t val;
    uint32_t sr;
    bool locked;
};

#endif
