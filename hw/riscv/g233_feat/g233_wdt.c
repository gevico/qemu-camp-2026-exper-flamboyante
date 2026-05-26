#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "g233_wdt.h"

static void g233_wdt_update_irq(G233WDTState *s)
{
    qemu_set_irq(s->irq, (s->sr & G233_WDT_SR_TIMEOUT) &&
                 (s->ctrl & G233_WDT_CTRL_INTEN));
}

static uint32_t g233_wdt_get_val(G233WDTState *s)
{
    s->val = ptimer_get_count(s->timer);
    return s->val;
}

static void g233_wdt_reload(G233WDTState *s)
{
    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    ptimer_set_count(s->timer, s->load);
    if(s->ctrl & G233_WDT_CTRL_EN)
    {
        ptimer_run(s->timer, 1);
    }
    ptimer_transaction_commit(s->timer);
    s->val = s->load;
}

static void g233_wdt_timeout(void *opaque)
{
    G233WDTState *s = G233_WDT(opaque);

    s->sr |= G233_WDT_SR_TIMEOUT;
    g233_wdt_update_irq(s);
}

static uint64_t g233_wdt_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233WDTState *s = G233_WDT(opaque);

    (void)size;

    switch (offset) {
    case G233_WDT_CTRL:
        return s->ctrl;
    case G233_WDT_LOAD:
        return s->load;
    case G233_WDT_VAL:
        return g233_wdt_get_val(s);
    case G233_WDT_KEY:
        return 0;
    case G233_WDT_SR:
        return s->sr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 WDT read from bad offset 0x%" HWADDR_PRIx "\n",
                      offset);
        return 0;
    }
}

static void g233_wdt_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned int size)
{
    G233WDTState *s = G233_WDT(opaque);
    uint32_t val = value;

    (void)size;

    switch (offset) {
    case G233_WDT_CTRL:
        if (!s->locked) {
            s->ctrl = val & (G233_WDT_CTRL_EN | G233_WDT_CTRL_INTEN);
            g233_wdt_reload(s);
        }
        break;
    case G233_WDT_LOAD:
        if (!s->locked) {
            s->load = val;
            s->val = val;
        }
        break;
    case G233_WDT_VAL:
        break;
    case G233_WDT_KEY:
        if (val == G233_WDT_KEY_FEED) {
            g233_wdt_reload(s);
        } else if (val == G233_WDT_KEY_LOCK) {
            s->locked = true;
        }
        break;
    case G233_WDT_SR:
        if (val & G233_WDT_SR_TIMEOUT) {
            s->sr &= ~G233_WDT_SR_TIMEOUT;
            g233_wdt_update_irq(s);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 WDT write to bad offset 0x%" HWADDR_PRIx "\n",
                      offset);
        break;
    }
}

static const MemoryRegionOps g233_wdt_ops = {
    .read = g233_wdt_read,
    .write = g233_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void g233_wdt_reset(DeviceState *dev)
{
    G233WDTState *s = G233_WDT(dev);

    if (s->timer) {
        ptimer_transaction_begin(s->timer);
        ptimer_stop(s->timer);
        ptimer_transaction_commit(s->timer);
    }

    s->ctrl = 0;
    s->load = 0;
    s->val = 0;
    s->sr = 0;
    s->locked = false;
    g233_wdt_update_irq(s);
}

static void g233_wdt_init(Object *obj)
{
    G233WDTState *s = G233_WDT(obj);

    memory_region_init_io(&s->mmio, obj, &g233_wdt_ops, s,
                          TYPE_G233_WDT, G233_WDT_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void g233_wdt_realize(DeviceState *dev, Error **errp)
{
    G233WDTState *s = G233_WDT(dev);

    (void)errp;

    s->timer = ptimer_init(g233_wdt_timeout, s, PTIMER_POLICY_LEGACY);
    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, G233_WDT_FREQ_HZ);
    ptimer_transaction_commit(s->timer);
}

static void g233_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g233_wdt_realize;
    device_class_set_legacy_reset(dc, g233_wdt_reset);
    dc->desc = "G233 WDT";
}

static const TypeInfo g233_wdt_info = {
    .name = TYPE_G233_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WDTState),
    .instance_init = g233_wdt_init,
    .class_init = g233_wdt_class_init,
};

static void g233_wdt_register_types(void)
{
    type_register_static(&g233_wdt_info);
}

type_init(g233_wdt_register_types)
