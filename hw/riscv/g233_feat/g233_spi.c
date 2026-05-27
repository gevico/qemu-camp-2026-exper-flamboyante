#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "g233_spi.h"

static void g233_spi_update_irq(G233SPIState *s)
{
    /*
     * TODO(day4-spi-irq): 汇总 TXEIE/TXE、RXNEIE/RXNE、ERRIE/OVERRUN，
     * 并把结果驱动到 PLIC IRQ 5。
     */
    if((s->cr1 & G233_SPI_CR1_SPE) == 0)
    {
        return;
    }
    bool irq = false;
    irq = (s->cr1 & G233_SPI_CR1_TXEIE) && (s->sr & G233_SPI_SR_TXE);
    irq |= (s->cr1 & G233_SPI_CR1_RXNEIE) && (s->sr & G233_SPI_SR_RXNE);
    irq |= (s->cr1 & G233_SPI_CR1_ERRIE) && (s->sr & G233_SPI_SR_OVERRUN);
    qemu_set_irq(s->irq, irq);
}

static void g233_spi_flash_reset_transaction(G233SPIFlash *flash)
{
    flash->cmd = 0;
    flash->addr = 0;
    flash->addr_bytes = 0;
    flash->jedec_pos = 0;
    flash->program_touched = false;
    flash->erase_pending = false;
    flash->erase_addr = 0;
    flash->phase = G233_SPI_FLASH_PHASE_IDLE;
}

static void g233_spi_flash_busy_done(void *opaque)
{
    G233SPIFlash *flash = opaque;

    if(flash->program_touched == false && flash->erase_pending == false)
    {
        return;
    }

    /*
    * TODO(day4-spi-flash): 实现 BUSY/WEL 生命周期。这里应清 BUSY，
    * 并在 program/erase 完成后清 WEL。
    */
    flash->status &= ~G233_SPI_FLASH_SR_BUSY;
    flash->status &= ~G233_SPI_FLASH_SR_WEL;
    if(flash->cmd == G233_SPI_FLASH_CMD_PAGE_PROGRAM)
    {
        flash->program_touched = false;
    }
    else if(flash->cmd == G233_SPI_FLASH_CMD_SECTOR_ERASE)
    {
        flash->erase_pending = false;
    }
    flash->phase = G233_SPI_FLASH_PHASE_IDLE;
    
}

static uint8_t g233_spi_flash_deal_cmd(G233SPIFlash *flash, uint8_t cmd)
{
    /*
     * TODO(day4-spi-flash): 处理 JEDEC、READ_STATUS、WRITE_ENABLE、
     * READ_DATA、PAGE_PROGRAM、SECTOR_ERASE 命令。
     */
    flash->cmd = cmd;
    switch (cmd)
    {
    case G233_SPI_FLASH_CMD_JEDEC_ID:
        flash->phase = G233_SPI_FLASH_PHASE_JEDEC;
        break;
    case G233_SPI_FLASH_CMD_READ_STATUS:
        flash->phase = G233_SPI_FLASH_PHASE_STATUS;
        break;
    case G233_SPI_FLASH_CMD_WRITE_ENABLE:
        flash->status |= G233_SPI_FLASH_SR_WEL; //WEL为1代表可以写
        flash->phase = G233_SPI_FLASH_PHASE_IDLE;
        break;
    case G233_SPI_FLASH_CMD_READ_DATA:
    case G233_SPI_FLASH_CMD_PAGE_PROGRAM:
    case G233_SPI_FLASH_CMD_SECTOR_ERASE:
        flash->phase = G233_SPI_FLASH_PHASE_ADDR;
        break;
    default:
        flash->phase = G233_SPI_FLASH_PHASE_IDLE;   
        break;
    }
    return 0xff;
}


static uint8_t g233_spi_flash_deal_jedec(G233SPIFlash *flash)
{
    /*
     * TODO(day4-spi-flash): 处理 3 位 JEDEC ID。
     */
    uint8_t ret = flash->jedec[flash->jedec_pos];
    flash->jedec_pos++;
    if (flash->jedec_pos >= 3) {
        flash->jedec_pos = 0;
        flash->phase = G233_SPI_FLASH_PHASE_IDLE;
    }
    return ret;
}

static uint8_t g233_spi_flash_deal_status(G233SPIFlash *flash)
{
    /*
     * TODO(day4-spi-flash): 处理 STATUS 字节。
     */
    uint8_t ret = flash->status;
    flash->phase = G233_SPI_FLASH_PHASE_IDLE;
    return ret;
}


static uint8_t g233_spi_flash_deal_addr(G233SPIFlash *flash, uint8_t tx)
{
    /*
     * TODO(day4-spi-flash): 处理 32位地址。
     */
    flash->addr_bytes++;
    flash->addr |= (tx << (flash->addr_bytes * 8));
    if (flash->addr_bytes >= 3) {
        flash->addr_bytes = 0;
        switch (flash->cmd)
        {
        case G233_SPI_FLASH_CMD_PAGE_PROGRAM:
            flash->phase = G233_SPI_FLASH_PHASE_PROGRAM;
            break;
        case G233_SPI_FLASH_CMD_SECTOR_ERASE:
            flash->erase_addr = flash->addr;
            flash->phase = G233_SPI_FLASH_PHASE_ERASE;
            break;
        case G233_SPI_FLASH_CMD_READ_DATA:
            flash->phase = G233_SPI_FLASH_PHASE_READ_DATA;
            break;  
        default:
            break;
        }
    }
    return 0xff;
}


static uint8_t g233_spi_flash_deal_read_data(G233SPIFlash *flash)
{
    /*
     * TODO(day4-spi-flash): 处理 READ_DATA 字节。
     */
    if(flash->addr >= flash->size)
    {
        return 0xff;
    }   
    uint8_t ret = flash->storage[flash->addr];
    flash->addr++;
    return ret;
}

static uint8_t g233_spi_flash_deal_program(G233SPIFlash *flash, uint8_t tx)
{
    /*
     * TODO(day4-spi-flash): 处理 PAGE_PROGRAM 字节。
     * - 检查地址是否超出范围
     * - 检查 WEL 是否为 1，检查是否busy
     * - 设置 busy
     * - 启动 program_touched
     * 
     * - 增加地址
     */
    if(flash->addr >= flash->size)
    {
        return 0xff;
    }
    if((flash->status & G233_SPI_FLASH_SR_WEL) == 0)
    {
        return 0xff;
    }
    if((flash->status & G233_SPI_FLASH_SR_BUSY) != 0)
    {
        return 0xff;
    }

    //flash->status |= G233_SPI_FLASH_SR_BUSY;
    flash->storage[flash->addr] &= tx;
    flash->addr++;
    // int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    // timer_mod(flash->busy_timer, now + 1000 *1000);
    flash->program_touched = true;
    return 0xff;
}

static uint8_t g233_spi_flash_deal_erase(G233SPIFlash *flash)
{
    if(flash->erase_addr >= flash->size)
    {
        return 0xff;
    }
    if((flash->status & G233_SPI_FLASH_SR_WEL) == 0)
    {
        return 0xff;
    }
    if((flash->status & G233_SPI_FLASH_SR_BUSY) != 0)
    {
        return 0xff;
    }

    flash->storage[flash->erase_addr] = 0xff;
    flash->erase_pending = true;
    return 0xff;

}


static uint8_t g233_spi_flash_xfer(G233SPIFlash *flash, uint8_t tx)
{
    
    uint8_t ret = 0xff;
    /*
     * TODO(day4-spi-flash): 实现 JEDEC、READ_STATUS、WRITE_ENABLE、
     * READ_DATA、PAGE_PROGRAM、SECTOR_ERASE 的逐字节状态机。
     */
    switch (flash->phase)
    {
    case G233_SPI_FLASH_PHASE_IDLE:
        /* code */
        ret = g233_spi_flash_deal_cmd(flash, tx);
        break;
    case G233_SPI_FLASH_PHASE_JEDEC:
        ret = g233_spi_flash_deal_jedec(flash);
        break;
    case G233_SPI_FLASH_PHASE_STATUS:
        ret = g233_spi_flash_deal_status(flash);
        break;
    case G233_SPI_FLASH_PHASE_ADDR:
        ret = g233_spi_flash_deal_addr(flash, tx);
        break;
    case G233_SPI_FLASH_PHASE_READ_DATA:
        ret = g233_spi_flash_deal_read_data(flash);
        break;
    case G233_SPI_FLASH_PHASE_PROGRAM:
        ret = g233_spi_flash_deal_program(flash, tx);
        break;
    case G233_SPI_FLASH_PHASE_ERASE:
        ret = g233_spi_flash_deal_erase(flash);
        break;
    default:
        break;
    }
    return ret;
}

static void g233_spi_flash_cs_deassert(G233SPIFlash *flash)
{
    /*
     * TODO(day4-spi-cs): CS 切换时 finalize 当前 transaction。后续需要在
     * 这里收尾 PAGE_PROGRAM/SECTOR_ERASE，并启动 busy_timer。
     */
    
    if((flash->status & G233_SPI_FLASH_SR_WEL) != 0)
    {
        flash->status |= G233_SPI_FLASH_SR_BUSY;
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        timer_mod(flash->busy_timer, now + 100000);
    }
    g233_spi_flash_reset_transaction(flash);
}

static void g233_spi_transfer_done(void *opaque)
{
    G233SPIState *s = G233_SPI(opaque);

    /*
     * TODO(day4-spi-xfer): 完成一次 byte transfer：
     * - 按 CR2 选择 CS0/CS1；
     * - 调用 g233_spi_flash_xfer() 得到 rx；
     * - 处理 RXNE/TXE/OVERRUN；
     * - 调用 g233_spi_update_irq()。
     */
    unsigned cs = s->cr2 & G233_SPI_CR2_CS_MASK;
    if (cs < G233_SPI_NR_FLASH) {
        s->rx = g233_spi_flash_xfer(&s->flash[cs], s->tx);
    } else {
        s->rx = 0xff;
    }

    if (s->rx & G233_SPI_SR_RXNE)
    {
        s->sr |= G233_SPI_SR_OVERRUN;
    }

    s->sr |= G233_SPI_SR_RXNE;
    s->sr |= G233_SPI_SR_TXE;

    g233_spi_update_irq(s);
}

static uint64_t g233_spi_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233SPIState *s = G233_SPI(opaque);

    (void)size;

    switch (offset) {
    case G233_SPI_CR1:
        return s->cr1;
    case G233_SPI_CR2:
        return s->cr2;
    case G233_SPI_SR:
        return s->sr;
    case G233_SPI_DR:
        /*
         * TODO(day4-spi-controller): 读 DR 应返回 rx，并清 RXNE。
         */
        s->sr &= ~G233_SPI_SR_RXNE;
        g233_spi_update_irq(s);
        return s->rx;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 SPI read from bad offset 0x%" HWADDR_PRIx "\n",
                      offset);
        return 0;
    }
}

static void g233_spi_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned int size)
{
    G233SPIState *s = G233_SPI(opaque);
    uint32_t val = value;
    unsigned old_cs;
    unsigned new_cs;

    (void)size;

    switch (offset) {
    case G233_SPI_CR1:
        s->cr1 = val & G233_SPI_CR1_MASK;
        g233_spi_update_irq(s);
        break;
    case G233_SPI_CR2:
        old_cs = s->cr2 & G233_SPI_CR2_CS_MASK;
        new_cs = val & G233_SPI_CR2_CS_MASK;
        if (old_cs < G233_SPI_NR_FLASH && old_cs != new_cs) {
            g233_spi_flash_cs_deassert(&s->flash[old_cs]);
        }
        s->cr2 = new_cs;
        break;
    case G233_SPI_SR:
        /*
         * TODO(day4-spi-overrun): SPI_SR.OVERRUN 是 W1C，其它状态位由
         * controller 行为维护。
         */
        s->sr &= ~(val & G233_SPI_SR_OVERRUN);
        g233_spi_update_irq(s);
        break;
    case G233_SPI_DR:
    {
        /*
         * TODO(day4-spi-controller): 写 DR 应保存低 8 位 tx，清 TXE，并
         * 用 xfer_timer 安排一次 transfer。
         */
        if (!(s->cr1 & G233_SPI_CR1_SPE)) 
        {
            return;
        }

        if (!(s->sr & G233_SPI_SR_TXE)) 
        {
            qemu_log_mask(LOG_GUEST_ERROR,
                        "G233 SPI write DR while TXE=0\n");
            return;
        }   


        s->tx = val;
        s->sr &= ~G233_SPI_SR_TXE;
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        timer_mod(s->xfer_timer, now + 1000);
        g233_spi_update_irq(s);
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "G233 SPI write to bad offset 0x%" HWADDR_PRIx "\n",
                      offset);
        break;
    }
}

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void g233_spi_flash_init(G233SPIFlash *flash, uint32_t size,
                                uint8_t jedec0, uint8_t jedec1,
                                uint8_t jedec2)
{
    flash->size = size;
    flash->jedec[0] = jedec0;
    flash->jedec[1] = jedec1;
    flash->jedec[2] = jedec2;
    flash->storage = g_malloc0(size);
    memset(flash->storage, 0xff, size);
    flash->busy_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     g233_spi_flash_busy_done, flash);
    flash->status = 0;
    g233_spi_flash_reset_transaction(flash);
}

static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);

    s->cr1 = 0;
    s->cr2 = 0;
    s->sr = G233_SPI_SR_TXE;
    s->tx = 0;
    s->rx = 0;

    if (s->xfer_timer) {
        timer_del(s->xfer_timer);
    }

    for (int i = 0; i < G233_SPI_NR_FLASH; i++) {
        if (s->flash[i].busy_timer) {
            timer_del(s->flash[i].busy_timer);
        }
        s->flash[i].status = 0;
        g233_spi_flash_reset_transaction(&s->flash[i]);
    }

    g233_spi_update_irq(s);
}

static void g233_spi_init(Object *obj)
{
    G233SPIState *s = G233_SPI(obj);

    memory_region_init_io(&s->mmio, obj, &g233_spi_ops, s,
                          TYPE_G233_SPI, G233_SPI_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    G233SPIState *s = G233_SPI(dev);

    (void)errp;

    s->xfer_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 g233_spi_transfer_done, s);
    g233_spi_flash_init(&s->flash[0], G233_SPI_FLASH_CS0_SIZE,
                        0xef, 0x30, 0x15);
    g233_spi_flash_init(&s->flash[1], G233_SPI_FLASH_CS1_SIZE,
                        0xef, 0x30, 0x16);
    g233_spi_reset(dev);
}

static void g233_spi_unrealize(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);

    if (s->xfer_timer) {
        timer_free(s->xfer_timer);
        s->xfer_timer = NULL;
    }

    for (int i = 0; i < G233_SPI_NR_FLASH; i++) {
        if (s->flash[i].busy_timer) {
            timer_free(s->flash[i].busy_timer);
            s->flash[i].busy_timer = NULL;
        }
        g_free(s->flash[i].storage);
        s->flash[i].storage = NULL;
    }
}

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g233_spi_realize;
    dc->unrealize = g233_spi_unrealize;
    device_class_set_legacy_reset(dc, g233_spi_reset);
    dc->desc = "G233 SPI";
}

static const TypeInfo g233_spi_info = {
    .name = TYPE_G233_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SPIState),
    .instance_init = g233_spi_init,
    .class_init = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}

type_init(g233_spi_register_types)
