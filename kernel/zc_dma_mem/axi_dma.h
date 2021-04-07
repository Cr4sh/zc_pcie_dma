#ifndef _AXI_DMA_
#define _AXI_DMA_

// number of DMA registers for each channel
#define DMA_REG_NUM 12

// DMA engine registers
#define DMA_REG_CONTROL  0x00
#define DMA_REG_STATUS   0x01
#define DMA_REG_ADDR_LO  0x06
#define DMA_REG_ADDR_HI  0x07
#define DMA_REG_LENGTH   0x0a

// DMA engine control register bits
#define DMA_CR_START     0x00000001
#define DMA_CR_RESET     0x00000004
#define DMA_CR_IRQEN_IOC 0x00001000 // enable completion interrupt
#define DMA_CR_IRQEN_DLY 0x00002000 // enable delay interrupt
#define DMA_CR_IRQEN_ERR 0x00004000 // enable error interrupt

// AMD engine status register bits
#define DMA_ST_HALTED    0x00000001
#define DMA_ST_IDLE      0x00000002
#define DMA_ST_ERR_INT   0x00000010 // internal error
#define DMA_ST_ERR_SLV   0x00000020 // slave error
#define DMA_ST_ERR_DEC   0x00000040 // decode error

#endif 
