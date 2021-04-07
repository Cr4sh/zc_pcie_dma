#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/of_platform.h>
#include <linux/byteorder/generic.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

#include "tlp.h"
#include "axi_dma.h"
#include "zc_dma_mem.h"

MODULE_LICENSE("GPL");

// class name, device name and access mode
#define DEVICE_CLASS "zc_dma_mem"
#define DEVICE_NAME  "zc_dma_mem_%d"
#define DEVICE_MODE  0666

// number of devices to create
#define DEVICE_COUNT 2

// zc_dma_mem_X devices by their functionality
#define DEVICE_NUM_MEM 0
#define DEVICE_NUM_TLP 1

// device tree type
#define DT_TYPE "generic-uio"

// device tree device names
#define DT_NAME_DMA_0 "dma_0"
#define DT_NAME_DMA_1 "dma_1"
#define DT_NAME_GPIO "gpio"

#define DMA_MEM_SIZE (PAGE_SIZE * 2)

// address and size alignment for mem_read() and mem_write()
#define DMA_MEM_ALIGN sizeof(unsigned int)

// how many seconds to wait for DMA transfer to complete
#define DMA_TIMEOUT 1

// TLP data length for mem_read()
#define MAX_TLP_LEN 0x10

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define ALIGN_DOWN(n, m) ((n) / (m) * (m))
#define ALIGN_UP(n, m) ALIGN_DOWN((n) + (m) - 1, (m))

struct device_data 
{
    struct cdev cdev;
};

struct device_context
{
    unsigned long long addr;
};

// character device variables
static int dev_major = 0;
static struct class *zc_dma_mem_dev_class = NULL;
static struct device_data zc_dma_mem_dev_data[DEVICE_COUNT];

// memory region for DMA I/O
static dma_addr_t addr_phys = 0;
static unsigned char *addr_virt = NULL;

// TLP transmit/receive buffer physical address
static unsigned long long addr_phys_tx = 0;
static unsigned long long addr_phys_rx = 0;

// TLP transmit/receive buffer virtual address
static unsigned int *addr_virt_tx = 0;
static unsigned int *addr_virt_rx = 0;

// devices MMIO regions
static unsigned int *dev_addr_dma_0 = NULL;
static unsigned int *dev_addr_dma_1 = NULL;
static unsigned int *dev_addr_dma_0_tx = NULL;
static unsigned int *dev_addr_dma_0_rx = NULL;
static unsigned int *dev_addr_dma_1_tx = NULL;
static unsigned int *dev_addr_dma_1_rx = NULL;
static unsigned int *dev_addr_gpio = NULL;

// TLP tag used in mem_read()
static unsigned char mem_read_tag = 0;

static spinlock_t dma_dev_lock;

static inline void get_device_id(struct device_id *dev_id)
{
    // read PCI-E device ID from GPIO register
    dev_id->val = ioread32(dev_addr_gpio);
}

static inline unsigned int dma_reg_read(unsigned int *dma_dev, int reg_num)
{
    // read DMA engine register by its number
    return ioread32(dma_dev + reg_num);
}

static inline void dma_reg_write(unsigned int *dma_dev, int reg_num, unsigned int reg_val)
{
    // read DMA engine register by its number
    iowrite32(reg_val, dma_dev + reg_num);
}

static void dma_reset(unsigned int *dma_dev)
{
    dma_reg_write(dma_dev, DMA_REG_CONTROL, DMA_CR_RESET);
    dma_reg_write(dma_dev, DMA_REG_CONTROL, 0);

    while (true)
    {                
        // check if reset is completed
        if ((dma_reg_read(dma_dev, DMA_REG_STATUS) & DMA_ST_HALTED) != 0)
        {
            break;
        }
    }
}

static int dma_transfer(unsigned int *dma_dev, unsigned long long addr, unsigned int size)
{
    unsigned int status = 0;
    unsigned long started = jiffies;

    // set up buffer address
    dma_reg_write(dma_dev, DMA_REG_ADDR_HI, (unsigned int)(addr >> 32));
    dma_reg_write(dma_dev, DMA_REG_ADDR_LO, (unsigned int)(addr & 0xffffffff));

    // start transfer
    dma_reg_write(dma_dev, DMA_REG_CONTROL, DMA_CR_START);
    dma_reg_write(dma_dev, DMA_REG_LENGTH, size);    

    while (true)
    {
        status = dma_reg_read(dma_dev, DMA_REG_STATUS);

        // check if transaction is completed
        if ((status & DMA_ST_IDLE) != 0 || (status & DMA_ST_HALTED) != 0)
        {
            break;
        }

        // check for timeout
        if (jiffies - started > (DMA_TIMEOUT * HZ))
        {
            // reset DMA channel
            dma_reset(dma_dev);

            printk(KERN_ERR "dma_transfer() fails, timeout occurred\n");
            return -ETIME; 
        }
    }

    // check for the error bits
    if ((status & (DMA_ST_ERR_INT | DMA_ST_ERR_SLV | DMA_ST_ERR_DEC)) != 0)
    {
        printk(KERN_ERR "dma_transfer() fails, status = 0x%.8x\n", status);
        return -EFAULT;
    }

    return 0;
}

static int tlp_recv(unsigned int *ret_size)
{    
    int size = 0, err = 0;

    // perform DMA transfer to receive TLP
    if ((err = dma_transfer(dev_addr_dma_0_rx, addr_phys_rx, PAGE_SIZE)) != 0)
    {
        return err;
    }

    // get transfer length
    size = dma_reg_read(dev_addr_dma_0_rx, DMA_REG_LENGTH);

    if (size % sizeof(unsigned int) != 0)
    {
        printk(KERN_ERR "tlp_recv() ERROR: Size is not aligned\n");
        return -EFAULT;
    }

    if (ret_size)
    {
        *ret_size = size / sizeof(unsigned int);
    }

    return 0;
}

static int tlp_send(unsigned int size)
{   
    int err = 0;

    // perform DMA transfer to send TLP
    if ((err = dma_transfer(dev_addr_dma_0_tx, addr_phys_tx, size * sizeof(unsigned int))) != 0)
    {
        return err;
    }

    return 0;
}

static unsigned int cfg_read(unsigned int addr, unsigned int *data)
{   
    int err = 0;

    // put address two times to assert cfg_mgmt_rd_en for two clock cycles
    addr_virt_tx[0] = addr;
    addr_virt_tx[1] = addr;

    // perform DMA transfer to send config space address
    if ((err = dma_transfer(dev_addr_dma_1_tx, addr_phys_tx, 2 * sizeof(unsigned int))) != 0)
    {
        return err;
    }

    // perform DMA transfer to receive config space data
    if ((err = dma_transfer(dev_addr_dma_1_rx, addr_phys_rx, PAGE_SIZE)) != 0)
    {
        return err;
    }

    // get transfer length
    unsigned int size = dma_reg_read(dev_addr_dma_1_rx, DMA_REG_LENGTH);

    if (size != sizeof(unsigned int))
    {
        printk(KERN_ERR "cfg_read() ERROR: Bad size\n");
        return -EFAULT;
    }

    if (data)
    {
        *data = addr_virt_rx[0];
    }

    return 0;
}

static int mem_read(unsigned long long addr, unsigned char *buff, unsigned int size)
{
    struct device_id dev_id;
    unsigned int ptr = 0, err = 0;    

    if (addr % sizeof(unsigned int) != 0)
    {
        printk(KERN_ERR "mem_read() ERROR: Address is not aligned\n");
        return -EINVAL;
    }

    if (size % sizeof(unsigned int) != 0)
    {
        printk(KERN_ERR "mem_read() ERROR: Size is not aligned\n");
        return -EINVAL;
    }

    // get PCI-E device id
    get_device_id(&dev_id);

    if (dev_id.val == 0)
    {
        printk(KERN_ERR "mem_read() ERROR: PCI-E endpoint is not initialized\n");
        return -ENOTCONN;
    }
    
    while (ptr < size)
    {
        unsigned char tlp_tag = mem_read_tag;
        unsigned int tlp_size = 0, data_len = 0, received = 0;
        unsigned int read_len = 0, read_len_max = MAX_TLP_LEN;        

        if ((addr & 0xfff) != 0)
        {
            // memory read TLP must reside within the single memory page
            read_len_max = MIN(
                (unsigned int)(ALIGN_UP(addr, PAGE_SIZE) - addr) / sizeof(unsigned int),
                MAX_TLP_LEN
            );
        }

        read_len = MIN((size - ptr) / sizeof(unsigned int), read_len_max);

        // set TLP type and data size
        addr_virt_tx[0] = (TLP_TYPE_MRd64 << 24) | read_len;               

        // set requester ID, tag and byte enable flags
        addr_virt_tx[1] = (dev_id.val << 16) | (tlp_tag << 8) | 0xff;  

        // set physical memory address
        addr_virt_tx[2] = (unsigned int)(addr >> 32);
        addr_virt_tx[3] = (unsigned int)(addr & 0xffffffff);

        // send request
        if ((err = tlp_send(4)) != 0)
        {
            printk(KERN_ERR "mem_read() ERROR: tlp_send() fails for address 0x%llx\n", addr);
            return err;
        }

        while (received < read_len)
        {
            // receive reply
            if ((err = tlp_recv(&tlp_size)) != 0)
            {
                printk(KERN_ERR "mem_read() ERROR: tlp_recv() fails for address 0x%llx\n", addr);
                return err;
            }

            // check for the valid completion TLP
            if ((addr_virt_rx[0] >> 24) != TLP_TYPE_CplD)
            {
                printk(KERN_ERR "mem_read() ERROR: Bad completion status for address 0x%llx\n", addr);
                return -EFAULT;
            }

            // check for the valid completion tag
            if ((unsigned char)(addr_virt_rx[2] >> 8) != tlp_tag)
            {
                printk(
                    KERN_ERR "mem_read() ERROR: Bad completion tag (0x%.2x != 0x%.2x) for address 0x%llx\n", 
                    (unsigned char)(addr_virt_rx[2] >> 8), tlp_tag, addr
                );

                return -EFAULT;
            }

            data_len = tlp_size - 3;

            for (int i = 0; i < data_len; i += 1)
            {
                // copy data to the output buffer with reversed byte order
                *(unsigned int *)(buff + ptr + (i * sizeof(unsigned int))) = htonl(addr_virt_rx[i + 3]); 
            }

            received += data_len;
        }

        mem_read_tag += 1;

        addr += data_len * sizeof(unsigned int);
        ptr += data_len * sizeof(unsigned int);        
    }    

    return 0;
}

static int mem_write(unsigned long long addr, unsigned char *buff, unsigned int size)
{
    struct device_id dev_id;
    unsigned int ptr = 0, err = 0;    

    if (addr % sizeof(unsigned int) != 0)
    {
        printk(KERN_ERR "mem_write() ERROR: Address is not aligned\n");
        return -EINVAL;
    }

    if (size % sizeof(unsigned int) != 0)
    {
        printk(KERN_ERR "mem_write() ERROR: Size is not aligned\n");
        return -EINVAL;
    }

    // get PCI-E device id
    get_device_id(&dev_id);

    if (dev_id.val == 0)
    {
        printk(KERN_ERR "mem_write() ERROR: PCI-E endpoint is not initialized\n");
        return -ENOTCONN;
    }

    while (ptr < size)
    {
        // set TLP type and data size
        addr_virt_tx[0] = (TLP_TYPE_MWr64 << 24) | 1; 

        // set requester ID and byte enable flags
        addr_virt_tx[1] = (dev_id.val << 16) | 0xff;    

        // set physical memory address
        addr_virt_tx[2] = (unsigned int)(addr >> 32);
        addr_virt_tx[3] = (unsigned int)(addr & 0xffffffff);

        // data to write with reversed byte order
        addr_virt_tx[4] = htonl(*(unsigned int *)(buff + ptr));

        // send TLP
        if ((err = tlp_send(5)) != 0)
        {
            printk(KERN_ERR "mem_write() ERROR: tlp_send() fails for address 0x%llx\n", addr);
            return err;
        }

        addr += sizeof(unsigned int);
        ptr += sizeof(unsigned int);
    }

    return 0;
}

static int zc_dma_mem_dev_open(struct inode *inode, struct file *file)
{
    struct device_context *ctx = NULL;

#ifdef VERBOSE

    printk(KERN_INFO "zc_dma_mem_dev_open()\n");    

#endif

    // allocate context structure
    if ((file->private_data = kmalloc(sizeof(struct device_context), GFP_KERNEL)) == NULL)
    {
        printk(KERN_ERR "zc_dma_mem_dev_open() ERROR: kmalloc() fails\n");
        return -EFAULT;
    }

    ctx = (struct device_context *)file->private_data;
    ctx->addr = 0;

    spin_lock(&dma_dev_lock);

    // reset DMA engines
    dma_reset(dev_addr_dma_0_rx);
    dma_reset(dev_addr_dma_0_tx);
    dma_reset(dev_addr_dma_1_rx);
    dma_reset(dev_addr_dma_1_tx);

    spin_unlock(&dma_dev_lock);

    return 0;
}

static int zc_dma_mem_dev_release(struct inode *inode, struct file *file)
{

#ifdef VERBOSE

    printk(KERN_INFO "zc_dma_mem_dev_release()\n");

#endif

    if (file->private_data)
    {
        // free context structure
        kfree(file->private_data);
    }

    return 0;
}

static long zc_dma_mem_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct device_id dev_id;

    if (cmd == ZC_DMA_MEM_IOCTL_DMA_RESET)
    {
        spin_lock(&dma_dev_lock);

        // reset DMA engines
        dma_reset(dev_addr_dma_0_rx);
        dma_reset(dev_addr_dma_0_tx);
        dma_reset(dev_addr_dma_1_rx);
        dma_reset(dev_addr_dma_1_tx);

        spin_unlock(&dma_dev_lock);

        return 0;
    }
    else if (cmd == ZC_DMA_MEM_IOCTL_GET_DEVICE_ID)
    {
        void __user *arg_user = (void __user *)arg;

        // get PCI-E device id
        get_device_id(&dev_id);

        // copy data to the user buffer
        if (copy_to_user(arg_user, &dev_id, sizeof(struct device_id)) != 0) 
        {
            printk(KERN_ERR "zc_dma_mem_dev_ioctl() ERROR: copy_to_user() fails\n");
            return -EFAULT;
        }

        return 0;
    }
    else if (cmd == ZC_DMA_MEM_IOCTL_CONFIG_READ)
    {
        void __user *arg_user = (void __user *)arg;
        unsigned int cfg_addr = 0, cfg_data = 0;

        // copy address from the the user buffer
        if (copy_from_user(&cfg_addr, arg_user, sizeof(cfg_addr)) != 0) 
        {
            printk(KERN_ERR "zc_dma_mem_dev_ioctl() ERROR: copy_from_user() fails\n");
            return -EFAULT;
        }

        spin_lock(&dma_dev_lock);

        // read PCI-E config space register
        int err = cfg_read(cfg_addr, &cfg_data);

        spin_unlock(&dma_dev_lock);
        
        if (err != 0)
        {
            return -EFAULT;   
        }

        // copy data to the user buffer
        if (copy_to_user(arg_user, &cfg_data, sizeof(cfg_data)) != 0) 
        {
            printk(KERN_ERR "zc_dma_mem_dev_ioctl() ERROR: copy_to_user() fails\n");
            return -EFAULT;
        }

        return 0;
    }

    return -EFAULT;
}

static loff_t zc_dma_mem_dev_llseek(struct file *file, loff_t offset, int whence)
{    
    int device_num = MINOR(file->f_path.dentry->d_inode->i_rdev);
    struct device_context *ctx = (struct device_context *)file->private_data;    

    if (device_num == DEVICE_NUM_MEM)
    {
        if (whence == SEEK_SET)
        {
            // set offset from the beginning of the memory space
            ctx->addr = (unsigned long long)offset;
        }
        else if (whence == SEEK_CUR)
        {
            // set ofsset from the current position
            ctx->addr += (unsigned long long)offset;
        }
        else
        {
            printk(KERN_ERR "zc_dma_mem_dev_llseek() ERROR: Unsupported whence %d\n", whence);
            return -EFAULT;
        }

        // return current address
        return (loff_t)ctx->addr;
    }
    else if (device_num == DEVICE_NUM_TLP)
    {
        printk(KERN_ERR "zc_dma_mem_dev_llseek() ERROR: Unsupported operation\n");
    }

    return -EFAULT;
}

static ssize_t zc_dma_mem_dev_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    ssize_t ret = -EFAULT;
    int device_num = MINOR(file->f_path.dentry->d_inode->i_rdev);
    struct device_context *ctx = (struct device_context *)file->private_data;    

    if (device_num == DEVICE_NUM_MEM)
    {
        // address and size must be aligned by word boundary
        unsigned long long read_addr = ALIGN_DOWN(ctx->addr, DMA_MEM_ALIGN);
        unsigned int read_size = ALIGN_UP((unsigned int)(count + (ctx->addr - read_addr)), DMA_MEM_ALIGN);    

        // allocate memory contents buffer
        unsigned char *data = vmalloc(read_size);
        if (data == NULL)
        {
            printk(KERN_ERR "zc_dma_mem_dev_read() ERROR: vmalloc() fails\n");
            return -EFAULT;
        }

        spin_lock(&dma_dev_lock);

        // perform memory read operation
        int err = mem_read(read_addr, data, read_size);

        spin_unlock(&dma_dev_lock);
        
        if (err == 0)
        {
            // copy data to the user buffer
            if (copy_to_user(buf, data + (ctx->addr - read_addr), count) == 0) 
            {
                // increment current read/write address
                ctx->addr += count;

                ret = count;
            }
            else
            {
                printk(KERN_ERR "zc_dma_mem_dev_read() ERROR: copy_to_user() fails\n");
            }
        }

        vfree(data);    
    }
    else if (device_num == DEVICE_NUM_TLP)
    {
        size_t size = 0;
        unsigned int tlp_size = 0;     

        spin_lock(&dma_dev_lock);

        // receive TLP
        int err = tlp_recv(&tlp_size);

        spin_unlock(&dma_dev_lock);
        
        if (err != 0)
        {
            printk(KERN_ERR "zc_dma_mem_dev_read() ERROR: tlp_recv() fails\n");

            if (err == -ETIME)
            {
                // tell to the caller that timeout occurred
                return -ETIME;
            }

            return -EFAULT;
        }

        size = tlp_size * sizeof(unsigned int);

        if (count >= size)
        {
            // copy data to the user buffer
            if (copy_to_user(buf, addr_virt_rx, size) == 0)
            {
                ret = size;
            } 
            else
            {
                printk(KERN_ERR "zc_dma_mem_dev_read() ERROR: copy_to_user() fails\n");
            }
        }
        else
        {
            printk(KERN_ERR "zc_dma_mem_dev_read() ERROR: Insufficient buffer length\n");
        }        
    }

    return ret;
}

static ssize_t zc_dma_mem_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
    ssize_t ret = -EFAULT;
    int device_num = MINOR(file->f_path.dentry->d_inode->i_rdev);
    struct device_context *ctx = (struct device_context *)file->private_data;    

    if (device_num == DEVICE_NUM_MEM)
    {
        // address and size must be aligned by word boundary
        unsigned long long read_addr = ALIGN_DOWN(ctx->addr, DMA_MEM_ALIGN);
        unsigned int read_size = ALIGN_UP((unsigned int)(count + (ctx->addr - read_addr)), DMA_MEM_ALIGN);    

        // allocate memory contents buffer
        unsigned char *data = vmalloc(read_size);
        if (data == NULL)
        {
            printk(KERN_ERR "zc_dma_mem_dev_write() ERROR: vmalloc() fails\n");
            return -EFAULT;
        }

        spin_lock(&dma_dev_lock);

        // read existing memory contents
        int err = mem_read(read_addr, data, read_size);

        spin_unlock(&dma_dev_lock);
        
        if (err == 0)
        {
            // copy data from the user buffer
            if (copy_from_user(data + (ctx->addr - read_addr), buf, count) == 0) 
            {
                spin_lock(&dma_dev_lock);

                // write modified memory contents
                err = mem_write(read_addr, data, read_size);

                spin_unlock(&dma_dev_lock);

                if (err == 0)
                {
                    // increment current read/write address
                    ctx->addr += count;

                    ret = count;
                }
            }        
            else
            {
                printk(KERN_ERR "zc_dma_mem_dev_write() ERROR: copy_from_user() fails\n");
            }
        }

        vfree(data); 
    } 
    else if (device_num == DEVICE_NUM_TLP)
    {
        if (count % sizeof(unsigned int) != 0 || count > PAGE_SIZE)
        {
            printk(KERN_ERR "zc_dma_mem_dev_writ() ERROR: Bad TLP size\n");
            return -EINVAL;
        }

        // copy TLP from the user buffer
        if (copy_from_user(addr_virt_tx, buf, count) == 0) 
        {
            spin_lock(&dma_dev_lock);

            // send TLP
            int err = tlp_send((unsigned int)(count / sizeof(unsigned int)));

            spin_unlock(&dma_dev_lock);

            if (err != 0)
            {
                printk(KERN_ERR "zc_dma_mem_dev_write() ERROR: tlp_send() fails\n");

                if (err == -ETIME)
                {
                    // tell to the caller that timeout occurred
                    return -ETIME;
                }

                return -EFAULT;
            }

            ret = count;
        }        
        else
        {
            printk(KERN_ERR "zc_dma_mem_dev_write() ERROR: copy_from_user() fails\n");
        }
    }  

    return ret;
}

static int zc_dma_mem_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", DEVICE_MODE);

    return 0;
}

static unsigned long find_device_address(const char *dev_type, const char *dev_name)
{
    struct device_node *node = NULL;

    do
    {
        // iterate devices by type name
        if ((node = of_find_compatible_node(node, NULL, dev_type)) != NULL)
        {
            int ret = 0;

            // get name property
            const char *prop_name = of_get_property(node, "name", &ret);
            if (prop_name)
            {
                if (!strcmp(prop_name, dev_name))
                {
                    // get reg property
                    const char *prop_reg = of_get_property(node, "reg", &ret);
                    if (prop_reg)
                    {
                        // return device base
                        return htonl(*(unsigned long *)prop_reg);
                    }       

                    break;
                }
            }
        }

    } while (node != NULL);

    return 0;
}

static const struct file_operations zc_dma_mem_dev_fops = 
{
    .owner          = THIS_MODULE,
    .llseek         = zc_dma_mem_dev_llseek,
    .open           = zc_dma_mem_dev_open,
    .release        = zc_dma_mem_dev_release,
    .unlocked_ioctl = zc_dma_mem_dev_ioctl,
    .read           = zc_dma_mem_dev_read,
    .write          = zc_dma_mem_dev_write
};

static int zc_dma_mem_init(void)
{    
    int err = -1;
    unsigned long dev_addr_phys = 0;

#ifdef VERBOSE

    printk(KERN_INFO "zc_dma_mem_init()\n");

#endif

    spin_lock_init(&dma_dev_lock);

    // allocate DMA buffer
    if ((addr_virt = dma_alloc_coherent(NULL, DMA_MEM_SIZE, &addr_phys, GFP_KERNEL)) == 0)
    {
        printk(KERN_ERR "dma_alloc_coherent() fails\n");
        goto _end;
    }   

#ifdef VERBOSE

    printk(KERN_INFO "DMA buffer is at 0x%p (phys: 0x%x)\n", addr_virt, addr_phys); 

#endif

    addr_virt_tx = (unsigned int *)(addr_virt);
    addr_virt_rx = (unsigned int *)(addr_virt + PAGE_SIZE);

    addr_phys_tx = (unsigned long long)(addr_phys);
    addr_phys_rx = (unsigned long long)(addr_phys + PAGE_SIZE);

    // get device MMIO region physical address
    if ((dev_addr_phys = find_device_address(DT_TYPE, DT_NAME_DMA_0)) != 0)
    {
        // map MMIO region
        if ((dev_addr_dma_0 = ioremap_nocache(dev_addr_phys, PAGE_SIZE)) != NULL)
        {

#ifdef VERBOSE

            printk(KERN_INFO "DMA device base is 0x%p (phys: 0x%lx)\n", dev_addr_dma_0, dev_addr_phys);
#endif
            // DMA transmit channel MMIO address
            dev_addr_dma_0_tx = dev_addr_dma_0;

            // DMA receive channel MMIO address
            dev_addr_dma_0_rx = dev_addr_dma_0 + DMA_REG_NUM;
        }
        else
        {
            goto _end;
        }
    }
    else
    {
        printk(KERN_ERR "ERROR: Unable to find \"%s\" device\n", DT_NAME_DMA_0);
        goto _end;   
    }

    // get device MMIO region physical address
    if ((dev_addr_phys = find_device_address(DT_TYPE, DT_NAME_DMA_1)) != 0)
    {
        // map MMIO region
        if ((dev_addr_dma_1 = ioremap_nocache(dev_addr_phys, PAGE_SIZE)) != NULL)
        {

#ifdef VERBOSE

            printk(KERN_INFO "DMA device base is 0x%p (phys: 0x%lx)\n", dev_addr_dma_1, dev_addr_phys);
#endif
            // DMA transmit channel MMIO address
            dev_addr_dma_1_tx = dev_addr_dma_1;

            // DMA receive channel MMIO address
            dev_addr_dma_1_rx = dev_addr_dma_1 + DMA_REG_NUM;
        }
        else
        {
            goto _end;
        }
    }
    else
    {
        printk(KERN_ERR "ERROR: Unable to find \"%s\" device\n", DT_NAME_DMA_1);
        goto _end;   
    }

    // get device MMIO region physical address
    if ((dev_addr_phys = find_device_address(DT_TYPE, DT_NAME_GPIO)) != 0)
    {
        // map MMIO region
        if ((dev_addr_gpio = ioremap_nocache(dev_addr_phys, PAGE_SIZE)) != NULL)
        {

#ifdef VERBOSE

            printk(KERN_INFO "GPIO device base is 0x%p (phys: 0x%lx)\n", dev_addr_gpio, dev_addr_phys);
#endif
        }
        else
        {
            goto _end;
        }
    }
    else
    {
        printk(KERN_ERR "ERROR: Unable to find \"%s\" device\n", DT_NAME_GPIO);
        goto _end;   
    }

    dev_t dev;

    // allocate character devices range
    if ((err = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME)) != 0)
    {
        return err;
    }

#ifdef VERBOSE

    printk(KERN_INFO "Creating /dev/%s character device\n", DEVICE_NAME);

#endif

    dev_major = MAJOR(dev);

    // register device class
    zc_dma_mem_dev_class = class_create(THIS_MODULE, DEVICE_CLASS);
    zc_dma_mem_dev_class->dev_uevent = zc_dma_mem_dev_uevent;

    for (int i = 0; i < DEVICE_COUNT; i += 1) 
    {
        // initialize device
        cdev_init(&zc_dma_mem_dev_data[i].cdev, &zc_dma_mem_dev_fops);
        zc_dma_mem_dev_data[i].cdev.owner = THIS_MODULE;

        // add device file
        cdev_add(&zc_dma_mem_dev_data[i].cdev, MKDEV(dev_major, i), 1);
        device_create(zc_dma_mem_dev_class, NULL, MKDEV(dev_major, i), NULL, DEVICE_NAME, i);
    }

_end:

    if (err != 0)
    {
        // perform cleanup on error
        if (dev_addr_dma_0 != NULL)
        {
            iounmap(dev_addr_dma_0);
        }

        if (dev_addr_dma_1 != NULL)
        {
            iounmap(dev_addr_dma_1);
        }

        if (dev_addr_gpio != NULL)
        {
            iounmap(dev_addr_gpio);
        }

        if (addr_virt != 0)
        {
            dma_free_coherent(NULL, DMA_MEM_SIZE, addr_virt, addr_phys);
        }
    }

    return err;
}

static void zc_dma_mem_exit(void)
{

#ifdef VERBOSE

    printk(KERN_INFO "zc_dma_mem_exit()\n");

#endif

    if (dev_addr_dma_0 != NULL)
    {
        iounmap(dev_addr_dma_0);
    }

    if (dev_addr_dma_1 != NULL)
    {
        iounmap(dev_addr_dma_1);
    }

    if (dev_addr_gpio != NULL)
    {
        iounmap(dev_addr_gpio);
    }

    if (addr_virt != 0)
    {
        dma_free_coherent(NULL, DMA_MEM_SIZE, addr_virt, addr_phys);
    }

    for (int i = 0; i < DEVICE_COUNT; i += 1) 
    {
        // delete device
        device_destroy(zc_dma_mem_dev_class, MKDEV(dev_major, i));
    }

    // unregister device class
    class_unregister(zc_dma_mem_dev_class);
    class_destroy(zc_dma_mem_dev_class);

    // free character devices range
    unregister_chrdev_region(MKDEV(dev_major, 0), MINORMASK);
}

module_init(zc_dma_mem_init)
module_exit(zc_dma_mem_exit)
