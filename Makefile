OUTFILE = zc_pcie_dma/zc_pcie_dma.runs/impl_1/zc_pcie_dma.bit

project:
	vivado -mode batch -source zc_pcie_dma.tcl

bin:
	if [ -f zc_pcie_dma.bin ]; then rm zc_pcie_dma.bin; fi
	if [ -f $(OUTFILE).bin ]; then rm $(OUTFILE).bin; fi
	echo "all: { $(OUTFILE) }" > .bit_to_bin.bif
	bootgen -image .bit_to_bin.bif -arch zynq -process_bitstream bin
	test -f $(OUTFILE).bin && cp $(OUTFILE).bin zc_pcie_dma.bin
	test -f $(OUTFILE) && cp $(OUTFILE) zc_pcie_dma.bit

dts_gen:
	if [ -d devicetree/my_dts ]; then rm -r devicetree/my_dts; fi
	cd devicetree && xsct -eval "source build_dts.tcl; build_dts ../zc_pcie_dma.xsa"
	cd devicetree && xsct -eval "source build_dts.tcl; include_dtsi scratch_mem.dtsi"

dts_build:
	cd devicetree && gcc -I . -I my_dts -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp -o my_dts/system-top.dts.tmp my_dts/system-top.dts
	cd devicetree && dtc -I dts -O dtb -o ../devicetree.dtb my_dts/system-top.dts.tmp

uenv:
	test -f zc_pcie_dma.bit
	python -c 'print "bootargs=console=ttyPS0,115200 root=/dev/mmcblk0p2 rw earlyprintk rootfstype=ext4 rootwait devtmpfs.mount=0 mem=992M uio_pdrv_genirq.of_id=generic-uio"' > uEnv.txt
	python -c 'import os; print "load_fpga=fatload mmc 0 0x4000000 zc_pcie_dma.bit && fpga loadb 0 0x4000000 " + str(os.path.getsize("zc_pcie_dma.bit"))' >> uEnv.txt
	python -c 'print "load_image=fatload mmc 0 $${kernel_load_address} $${kernel_image} && fatload mmc 0 $${devicetree_load_address} devicetree.dtb"' >> uEnv.txt
	python -c 'print "uenvcmd=echo Copying Linux from SD to RAM... && mmcinfo && run load_fpga && run load_image && bootm $${kernel_load_address} - $${devicetree_load_address}"' >> uEnv.txt
