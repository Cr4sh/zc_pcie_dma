import sys, os, time, array, fcntl, unittest
from struct import pack, unpack


class DeviceMem(object):

    PATH = '/dev/zc_dma_mem_0'

    mem_write_1 = lambda self, addr, v: self.mem_write(addr, pack('B', v))
    mem_write_2 = lambda self, addr, v: self.mem_write(addr, pack('H', v))
    mem_write_4 = lambda self, addr, v: self.mem_write(addr, pack('I', v))
    mem_write_8 = lambda self, addr, v: self.mem_write(addr, pack('Q', v))

    mem_read_1 = lambda self, addr: unpack('B', self.mem_read(addr, 1))[0]
    mem_read_2 = lambda self, addr: unpack('H', self.mem_read(addr, 2))[0]
    mem_read_4 = lambda self, addr: unpack('I', self.mem_read(addr, 4))[0]
    mem_read_8 = lambda self, addr: unpack('Q', self.mem_read(addr, 8))[0]

    def __init__(self):

        # open target device
        self.fd = os.open(self.PATH, os.O_RDWR)

    def mem_read(self, addr, size):

        # set file pointer
        os.lseek(self.fd, addr, os.SEEK_SET)

        # read memory contents
        return os.read(self.fd, size)

    def mem_write(self, addr, data):

        # set file pointer
        os.lseek(self.fd, addr, os.SEEK_SET)

        # write memory contents
        os.write(self.fd, data)


class DeviceTlp(object):

    PATH = '/dev/zc_dma_mem_1'

    # buffer length for tlp_recv()
    RECV_BUFF_LEN = 0x1000

    ZC_DMA_MEM_IOCTL_RESET          = 0x0000cc00
    ZC_DMA_MEM_IOCTL_GET_DEVICE_ID  = 0x8004cc01
    ZC_DMA_MEM_IOCTL_CONFIG_READ    = 0xc004cc02

    def __init__(self):

        # open target device
        self.fd = os.open(self.PATH, os.O_RDWR)

    def reset(self):

        # send IOCTL request to the driver
        fcntl.ioctl(self.fd, self.ZC_DMA_MEM_IOCTL_RESET)

    def get_device_id(self):

        buff = array.array('I', [ 0 ])

        # send IOCTL request to the driver
        fcntl.ioctl(self.fd, self.ZC_DMA_MEM_IOCTL_GET_DEVICE_ID, buff, 1)

        return buff[0]    

    def cfg_read(self, addr):

        buff = array.array('I', [ addr ])

        # send IOCTL request to the driver
        fcntl.ioctl(self.fd, self.ZC_DMA_MEM_IOCTL_CONFIG_READ, buff, 1)

        return buff[0]    

    def tlp_send(self, data):

        data = ''.join(map(lambda dw: pack('I', dw), data))

        # send TLP
        os.write(self.fd, data)

    def tlp_recv(self):

        ret = []

        # receive TLP
        data = os.read(self.fd, self.RECV_BUFF_LEN)

        for i in range(0, len(data) / 4):

            ret.append(unpack('I', data[i * 4 : (i + 1) * 4])[0])        

        return ret

class TestMem(unittest.TestCase):

    TEST_ADDR = 0x1000

    def test_mem(self):

        dev = DeviceMem()

        data = dev.mem_read(self.TEST_ADDR, 0x100)

        dev.mem_write(self.TEST_ADDR, data)

    def test_normal(self, addr = TEST_ADDR):

        dev = DeviceMem()

        val = 0x0102030405060708

        # backup old data
        old = dev.mem_read_8(addr)

        dev.mem_write_8(addr, val)

        assert dev.mem_read_1(addr) == val & 0xff
        assert dev.mem_read_2(addr) == val & 0xffff
        assert dev.mem_read_4(addr) == val & 0xffffffff
        assert dev.mem_read_8(addr) == val

        # restore old data
        dev.mem_write_8(addr, old)

    def test_unaligned(self, addr = TEST_ADDR):

        dev = DeviceMem()

        val = int(time.time())

        # backup old data
        old = dev.mem_read_8(addr)

        dev.mem_write_8(addr, 0)
        dev.mem_write_4(addr + 1, val)

        assert dev.mem_read_8(addr) == val << 8

        dev.mem_write_8(addr, 0)
        dev.mem_write_4(addr + 2, val)

        assert dev.mem_read_8(addr) == val << 16

        dev.mem_write_8(addr, 0)
        dev.mem_write_4(addr + 3, val)

        assert dev.mem_read_8(addr) == val << 24

        # restore old data
        dev.mem_write_8(addr, old)

    def test_cross_page(self):

        self.test_normal(addr = self.TEST_ADDR - 1)
        
        self.test_unaligned(addr = self.TEST_ADDR - 2)

        self.test_normal(addr = self.TEST_ADDR - 2)
        
        self.test_unaligned(addr = self.TEST_ADDR - 3)

        self.test_normal(addr = self.TEST_ADDR - 3)
        
        self.test_unaligned(addr = self.TEST_ADDR - 4) 

    def test_large(self, addr = TEST_ADDR):

        dev = DeviceMem()

        data = ''.join(map(lambda c: chr(c), range(0, 0x100)))

        # backup old data
        old = dev.mem_read(addr, len(data))

        dev.mem_write(addr, data)

        for i in range(0, len(data) - 1):

            assert dev.mem_read(addr + i, len(data) - i) == data[i :]

        # restore old data
        dev.mem_write(addr, old)


class TestTlp(unittest.TestCase):

    TEST_ADDR = 0x1000    

    def test_reset(self):

        dev = DeviceTlp()

        dev.reset()

    def test_device_id(self):

        dev = DeviceTlp()

        assert dev.get_device_id() != 0

    def test_cfg_read(self):

        dev = DeviceTlp()

        assert dev.cfg_read(0) == 0x133710ee

    def test_send_recv(self):

        dev = DeviceTlp()

        to_str = lambda tlp: ' '.join(map(lambda dw: '0x%.8x' % dw, tlp))

        # MRd TLP        
        tlp_tx = [ 0x20000001,
                   0x000000ff | (dev.get_device_id() << 16),                   
                   0x00000000,
                   self.TEST_ADDR ]    

        dev.tlp_send(tlp_tx)        

        tlp_rx = dev.tlp_recv()

        # check for CplD TLP
        assert (tlp_rx[0] >> 24) == 0x4a


if __name__ == '__main__':

    unittest.main()

#
# EoF
#
