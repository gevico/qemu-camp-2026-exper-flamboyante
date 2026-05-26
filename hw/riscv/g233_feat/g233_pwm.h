#ifndef G233_PWM_H
#define G233_PWM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"


#define TYPE_G233_PWM "g233-pwm"
typedef struct G233PWMState G233PWMState;
DECLARE_INSTANCE_CHECKER(G233PWMState, G233_PWM,
                         TYPE_G233_PWM)

#define G233_PWM_FREQ_HZ 1000

#define G233_PWM_SIZE   0x100
#define G233_PWM_CH_NUM     4

#define G233_PWM_GLB     0x00
#define G233_PWM_CH_BASE 0x10

#define CHn_CTRL         0X00
#define CHn_PERIOD       0X04
#define CHn_DUTY         0X08
#define CHn_COUNT        0X0C


typedef struct G233PWMChannel {
    uint32_t ctrl;
    uint32_t period;
    uint32_t duty;
    uint32_t cnt;
    bool done;
    QEMUTimer timer;
    int64_t last_update_ns;
} G233PWMChannel;



typedef struct G233PWMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    G233PWMChannel ch[G233_PWM_CH_NUM];
} G233PWMState;


                         
#endif
