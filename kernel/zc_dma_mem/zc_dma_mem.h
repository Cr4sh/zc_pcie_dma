#ifndef _ZC_DMA_MEM_
#define _ZC_DMA_MEM_

struct device_id
{
    union
    {
        struct
        {                        
            unsigned char func : 3;
            unsigned char dev : 5;
            unsigned char bus;
        };

        unsigned int val;
    };    

} __attribute__((__packed__));

#define ZC_DMA_MEM_IOCTL_MAGIC 0xcc

// IOCTLs definitions
#define ZC_DMA_MEM_IOCTL_DMA_RESET       _IO(ZC_DMA_MEM_IOCTL_MAGIC, 0)
#define ZC_DMA_MEM_IOCTL_GET_DEVICE_ID  _IOR(ZC_DMA_MEM_IOCTL_MAGIC, 1, struct device_id)
#define ZC_DMA_MEM_IOCTL_CONFIG_READ   _IOWR(ZC_DMA_MEM_IOCTL_MAGIC, 2, unsigned int)

#endif
