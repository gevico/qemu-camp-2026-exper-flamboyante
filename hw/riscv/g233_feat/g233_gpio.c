#include "qemu/osdep.h"
#include "qemu/log.h"
#include "g233_gpio.h"
#include "hw/core/irq.h"




static void g233_update_state(G233GPIOState *s)
{
    /*
     * 当前训练营测试没有外部设备驱动 GPIO 输入 pin，所以外部输入先建模
     * 为 0。完整模型可以在这里接入 qdev_init_gpio_in() 记录的输入值。
     */
    uint32_t external_in = 0;

    /*
     * GPIO_IN 表示当前 pin 电平：
     * - DIR=1 时，pin 处于输出模式，由 GPIO_OUT 驱动；
     * - DIR=0 时，pin 处于输入模式，由外部输入驱动。
     */
    uint32_t new_in = (s->out & s->dir) | (external_in & ~s->dir);

    uint32_t enabled = s->ie;
    uint32_t trig = s->trig;
    uint32_t pol = s->pol; /* 0=low/falling, 1=high/rising */
    uint32_t prev_in = s->prev_in;

    /*
     * 边沿检测：
     * rising_edge 为 1 的 bit 表示对应 pin 发生 0 -> 1；
     * falling_edge 为 1 的 bit 表示对应 pin 发生 1 -> 0。
     */
    uint32_t rising_edge = new_in & ~prev_in;
    uint32_t falling_edge = prev_in & ~new_in;

    /*
     * TRIG=0 表示边沿触发，所以取反后得到 edge_mask。
     * POL=1 选择 rising，POL=0 选择 falling。
     * GPIO_IE=1 的 pin 才允许置中断状态。
     */
    uint32_t edge_mask = ~trig;
    uint32_t edge_status = enabled & edge_mask &
        ((rising_edge & pol) | (falling_edge & ~pol));

    /*
     * TRIG=1 表示电平触发。POL=1 时高电平有效，POL=0 时低电平有效。
     * level 状态不是 sticky：电平不满足后要从 GPIO_IS 中清掉。
     */
    uint32_t level_mask = trig;
    uint32_t level_status = enabled & level_mask &
        ((new_in & pol) | (~new_in & ~pol));

    /* edge 中断是 sticky 的，出现一次边沿后保持到 guest 写 1 清除。 */
    s->is |= edge_status;

    /* level 中断按当前电平重算，只清 level 位，保留 edge sticky 位。 */
    s->is &= ~level_mask;
    s->is |= level_status;

    /* 保存当前 pin 电平，并作为下一次边沿检测的历史值。 */
    s->in = new_in;
    s->prev_in = new_in;

    /* 32 个 pin 汇总成一根 GPIO 中断线，接到 PLIC IRQ 2。 */
    qemu_set_irq(s->irq, (s->is & s->ie) != 0);
}



static uint64_t g233_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233GPIOState *s = G233_GPIO(opaque);
    uint64_t r = 0;

    (void)size;

    switch (offset) {
    case G233_GPIO_DIR:
        r = s->dir;
        break;
    case G233_GPIO_OUT:
        r = s->out;
        break;
    case G233_GPIO_IN:
        r = s->in;
        break;
    case G233_GPIO_IE:
        r = s->ie;
        break;
    case G233_GPIO_IS:
        r = s->is;
        break;
    case G233_GPIO_TRIG:
        r = s->trig;
        break;
    case G233_GPIO_POL:
        r = s->pol;
        break;
    default:
        r = 0;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 GPIO %s read from offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    /*
     * TODO: 未来如果需要稳定的 GPIO MMIO 调试输出，可以补 trace-events
     * 定义后打开这类 trace 调用。当前 qtest 不依赖 trace。
     */
    //trace_g233_gpio_read(offset, r);

    return r;
}


static void g233_gpio_write(void *opaque, hwaddr offset, uint64_t value, unsigned int size)
{
    (void)size;

    G233GPIOState *s = G233_GPIO(opaque);

    /*
     * TODO: 未来如果需要稳定的 GPIO MMIO 调试输出，可以补 trace-events
     * 定义后打开这类 trace 调用。当前 qtest 不依赖 trace。
     */
    //trace_g233_gpio_write(offset, value);

    switch (offset) {
    case G233_GPIO_DIR:
        s->dir = value;
        break;
    case G233_GPIO_OUT:
        s->out = value;
        break;
    case G233_GPIO_IN:
        break;
    case G233_GPIO_IE:
        s->ie = value;
        break;
    case G233_GPIO_IS:
        s->is &= ~value;    
        break;
    case G233_GPIO_TRIG:
        s->trig = value;
        break;  
    case G233_GPIO_POL:
        s->pol = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 GPIO %s write to offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }


    g233_update_state(s);

}


static const MemoryRegionOps g233_gpio_ops = {
    .read =  g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};


static void g233_gpio_reset(DeviceState  *dev)
{
    G233GPIOState *s = G233_GPIO(dev);
    s->dir = 0;
    s->out = 0;
    s->in = 0;
    s->ie = 0;
    s->is = 0;
    s->trig = 0;
    s->pol = 0;
    s->prev_in = 0;
    qemu_set_irq(s->irq, 0);
}

static void g233_gpio_realize(DeviceState *dev, Error **errp)
{
    G233GPIOState *s = G233_GPIO(dev);

    (void)errp;

    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_gpio_ops, s,
            TYPE_G233_GPIO, G233_GPIO_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    /*
     * TODO: 未来如果有其他虚拟外设要驱动或观察 G233 GPIO pin，
     * 可以补 qdev_init_gpio_in/out。当前 GPIO 测试只需要输出模式下
     * OUT 到 IN 的最小回读模型。
     */
    //qdev_init_gpio_in(DEVICE(s), sifive_gpio_set, s->ngpio);
    //qdev_init_gpio_out(DEVICE(s), s->output, s->ngpio);

}

static void g233_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /*
     * TODO: 当前 G233 GPIO 没有可配置属性，也不测试迁移状态。
     * 如果后续支持可配置 pin 数或 savevm/live migration，再补
     * Property 表和 VMStateDescription。
     */
    //device_class_set_props(dc, g233_gpio_properties);
    //dc->vmsd = &vmstate_g233_gpio;
    dc->realize = g233_gpio_realize;
    device_class_set_legacy_reset(dc, g233_gpio_reset);
    dc->desc = "G233 GPIO";
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .class_init = g233_gpio_class_init
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types)
