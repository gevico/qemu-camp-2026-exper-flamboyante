#include "qemu/osdep.h"
#include "qemu/log.h"
#include "g233_pwm.h"
#include "hw/core/irq.h"

static void g233_pwm_update_irq(G233PWMState *s)
{
    // TODO: 实现 PWM 中断更新逻辑
    qemu_set_irq(s->irq, 0);
}


static void g233_pwm_interrupt(G233PWMState *s, uint32_t channel)
{
    // TODO: 实现 PWM 中断逻辑
    s->ch[channel].done = true;
    g233_pwm_update_irq(s);
}


static void g233_pwm_interrupt_0(void *opaque)
{
    G233PWMState *s = G233_PWM(opaque);

    g233_pwm_interrupt(s, 0);
}

static void g233_pwm_interrupt_1(void *opaque)
{
    G233PWMState *s = G233_PWM(opaque);

    g233_pwm_interrupt(s, 1);
}

static void g233_pwm_interrupt_2(void *opaque)
{
    G233PWMState *s = G233_PWM(opaque);

    g233_pwm_interrupt(s, 2);
}

static void g233_pwm_interrupt_3(void *opaque)
{
    G233PWMState *s = G233_PWM(opaque);

    g233_pwm_interrupt(s, 3);
}


static uint64_t g233_pwm_get_glb(G233PWMState *s)
{
    uint64_t glb =0;
    for(int i=0;i<G233_PWM_CH_NUM;i++)
    {
        glb |= (s->ch[i].ctrl & 0x01) << (0 + i);
        glb |= (s->ch[i].done & 0x01) << (4 + i);
    }
    return glb;
}


static void g233_pwm_start_channel(G233PWMState *s, uint32_t channel)
{
    G233PWMChannel *ch = &s->ch[channel];
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t delay_ns;

    ch->last_update_ns = now;
    ch->cnt = 0;
    ch->done = false;

    timer_del(&ch->timer);

    if (ch->period == 0) {
        return;
    }

    delay_ns = muldiv64(ch->period,
                        NANOSECONDS_PER_SECOND,
                        G233_PWM_FREQ_HZ);

    timer_mod(&ch->timer, now + delay_ns);
}

static uint32_t g233_pwm_get_cnt(G233PWMChannel *ch)
{
    int64_t now;
    uint64_t elapsed_ticks;

    if (!(ch->ctrl & 0x1)) {
        return ch->cnt;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    elapsed_ticks = muldiv64(now - ch->last_update_ns,
                             G233_PWM_FREQ_HZ,
                             NANOSECONDS_PER_SECOND);

    if (ch->period != 0 && elapsed_ticks > ch->period) {
        return ch->period;
    }

    return elapsed_ticks;
}


static uint64_t g233_pwm_read(void *opaque, hwaddr addr,
                                 unsigned int size)
{
    // TODO: 实现 PWM 读取逻辑
    G233PWMState *s = G233_PWM(opaque);

    hwaddr offset = 0;
    uint32_t channel = 0;
    if(addr == G233_PWM_GLB)
    {
        offset = addr;
        return g233_pwm_get_glb(s);
    }
    else if(addr < G233_PWM_CH_BASE && addr > G233_PWM_GLB)
    {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 PWM %s read invalid addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }
    else
    {
        offset = (addr - G233_PWM_CH_BASE) % 0x10;
        channel = (addr - G233_PWM_CH_BASE) / 0x10;
        if(channel >= G233_PWM_CH_NUM)
        {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "G233 PWM %s read invalid addr 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            return 0;
        }
    }


    switch (offset)
    {
    case CHn_CTRL:
        return s->ch[channel].ctrl;
        break;
    case CHn_PERIOD:
        return s->ch[channel].period;
        break;
    case CHn_DUTY:
        return s->ch[channel].duty;
        break;
    case CHn_COUNT:
        return g233_pwm_get_cnt(&s->ch[channel]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 PWM %s read from offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
        break;
    }
}

static void g233_pwm_write(void *opaque, hwaddr addr,
                           uint64_t val64, unsigned int size)
{
    G233PWMState *s = G233_PWM(opaque);
    uint32_t value = val64;
    hwaddr offset;
    uint32_t channel;
    uint32_t old_ctrl;

    (void)size;

    if (addr == G233_PWM_GLB) {
        for (int i = 0; i < G233_PWM_CH_NUM; i++) {
            if (value & BIT(4 + i)) {
                s->ch[i].done = false;
            }
        }
        g233_pwm_update_irq(s);
        return;
    }

    if (addr < G233_PWM_CH_BASE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 PWM %s write invalid addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    offset = (addr - G233_PWM_CH_BASE) % 0x10;
    channel = (addr - G233_PWM_CH_BASE) / 0x10;

    if (channel >= G233_PWM_CH_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 PWM %s write invalid addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    switch (offset) {
    case CHn_CTRL:
        old_ctrl = s->ch[channel].ctrl;
        s->ch[channel].ctrl = value & (0x01 | 0x02);

        if (!(old_ctrl & 0x01) &&
            (s->ch[channel].ctrl & 0x01)) {
            g233_pwm_start_channel(s, channel);
        } else if ((old_ctrl & 0x01) &&
                   !(s->ch[channel].ctrl & 0x01)) {
            timer_del(&s->ch[channel].timer);
            s->ch[channel].cnt = 0;
        }
        break;

    case CHn_PERIOD:
        s->ch[channel].period = value;
        if (s->ch[channel].ctrl & 0x01) {
            g233_pwm_start_channel(s, channel);
        }
        break;

    case CHn_DUTY:
        s->ch[channel].duty = value;
        break;

    case CHn_COUNT:
        /* CNT is read-only. */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 PWM %s write invalid addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void g233_pwm_reset(DeviceState *dev)
{
    G233PWMState *s = G233_PWM(dev);
    for (int i = 0; i < G233_PWM_CH_NUM; i++)
    {
        timer_del(&s->ch[i].timer);
        s->ch[i].ctrl = 0;
        s->ch[i].period = 0;
        s->ch[i].duty = 0;
        s->ch[i].cnt = 0;
        s->ch[i].done = false;
        s->ch[i].last_update_ns = 0;
    }

    g233_pwm_update_irq(s);
}


static void g233_pwm_init(Object *obj)
{
    // TODO: 初始化 PWM 状态
    G233PWMState *s = G233_PWM(obj);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    memory_region_init_io(&s->mmio, obj, &g233_pwm_ops, s,
                          TYPE_G233_PWM, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void g233_pwm_realize(DeviceState *dev, Error **errp)
{
    // TODO: 实现 PWM 实现
    G233PWMState *s = G233_PWM(dev);

    timer_init_ns(&s->ch[0].timer, QEMU_CLOCK_VIRTUAL,
                  g233_pwm_interrupt_0, s);

    timer_init_ns(&s->ch[1].timer, QEMU_CLOCK_VIRTUAL,
                  g233_pwm_interrupt_1, s);

    timer_init_ns(&s->ch[2].timer, QEMU_CLOCK_VIRTUAL,
                  g233_pwm_interrupt_2, s);

    timer_init_ns(&s->ch[3].timer, QEMU_CLOCK_VIRTUAL,
                  g233_pwm_interrupt_3, s);
}




static void g233_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, g233_pwm_reset);
    //device_class_set_props(dc, g233_pwm_properties);
    //dc->vmsd = &vmstate_g233_pwm;
    dc->realize = g233_pwm_realize;
    dc->desc = "G233 PWM";
}

static const TypeInfo g233_pwm_info = {
    .name          = TYPE_G233_PWM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .instance_init = g233_pwm_init,
    .class_init    = g233_pwm_class_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types)
