#ifndef G233_SPI_H
#define G233_SPI_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "qom/object.h"

#define TYPE_G233_SPI "g233-spi"
typedef struct G233SPIState G233SPIState;
DECLARE_INSTANCE_CHECKER(G233SPIState, G233_SPI,
                         TYPE_G233_SPI)

#define G233_SPI_SIZE 0x100
#define G233_SPI_NR_FLASH 2

#define G233_SPI_CR1 0x00
#define G233_SPI_CR2 0x04
#define G233_SPI_SR  0x08
#define G233_SPI_DR  0x0c

#define G233_SPI_CR1_SPE    BIT(0)
#define G233_SPI_CR1_MSTR   BIT(2)
#define G233_SPI_CR1_ERRIE  BIT(5)
#define G233_SPI_CR1_RXNEIE BIT(6)
#define G233_SPI_CR1_TXEIE  BIT(7)
#define G233_SPI_CR1_MASK   (G233_SPI_CR1_SPE | G233_SPI_CR1_MSTR | \
                             G233_SPI_CR1_ERRIE | G233_SPI_CR1_RXNEIE | \
                             G233_SPI_CR1_TXEIE)

#define G233_SPI_CR2_CS_MASK 0x3u

#define G233_SPI_SR_RXNE    BIT(0)
#define G233_SPI_SR_TXE     BIT(1)
#define G233_SPI_SR_OVERRUN BIT(4)

#define G233_SPI_FLASH_CMD_WRITE_ENABLE  0x06
#define G233_SPI_FLASH_CMD_READ_STATUS   0x05
#define G233_SPI_FLASH_CMD_READ_DATA     0x03
#define G233_SPI_FLASH_CMD_PAGE_PROGRAM  0x02
#define G233_SPI_FLASH_CMD_SECTOR_ERASE  0x20
#define G233_SPI_FLASH_CMD_JEDEC_ID      0x9f

#define G233_SPI_FLASH_SR_BUSY BIT(0)
#define G233_SPI_FLASH_SR_WEL  BIT(1)

#define G233_SPI_FLASH_SECTOR_SIZE 4096
#define G233_SPI_FLASH_PAGE_SIZE   256
#define G233_SPI_FLASH_CS0_SIZE    (2 * MiB)
#define G233_SPI_FLASH_CS1_SIZE    (4 * MiB)

typedef enum G233SPIFlashPhase {
    G233_SPI_FLASH_PHASE_IDLE,
    G233_SPI_FLASH_PHASE_JEDEC,
    G233_SPI_FLASH_PHASE_ADDR,
    G233_SPI_FLASH_PHASE_READ,
    G233_SPI_FLASH_PHASE_PROGRAM,
    G233_SPI_FLASH_PHASE_STATUS,
} G233SPIFlashPhase;

typedef struct G233SPIFlash {
    uint8_t *storage;
    uint32_t size;
    uint8_t jedec[3];
    uint8_t status;
    uint8_t cmd;
    uint32_t addr;
    int addr_bytes;
    int jedec_pos;
    bool program_touched;
    bool erase_pending;
    uint32_t erase_addr;
    G233SPIFlashPhase phase;
    QEMUTimer *busy_timer;
} G233SPIFlash;

struct G233SPIState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t cr1;
    uint32_t cr2;
    uint32_t sr;
    uint8_t tx;
    uint8_t rx;

    QEMUTimer *xfer_timer;
    G233SPIFlash flash[G233_SPI_NR_FLASH];
};

#endif
