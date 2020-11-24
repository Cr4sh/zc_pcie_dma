OUTFILE = zc_pcie_dma/zc_pcie_dma.runs/impl_1/zc_pcie_dma.bit

project:
	vivado -mode batch -source zc_pcie_dma.tcl

bin:
	test -f zc_pcie_dma.bin && rm zc_pcie_dma.bin || /bin/true
	test -f $(OUTFILE).bin && rm $(OUTFILE).bin || /bin/true
	bootgen -image bit_to_bin.bif -arch zynq -process_bitstream bin
	test -f $(OUTFILE).bin && cp $(OUTFILE).bin zc_pcie_dma.bin
	test -f $(OUTFILE) && cp $(OUTFILE) zc_pcie_dma.bit

dts_gen:
	test -f devicetree/my_dts && rm -r devicetree/my_dts || /bin/true
	cd devicetree && xsct -eval "source build_dts.tcl; build_dts ../zc_pcie_dma.xsa"
	cd devicetree && xsct -eval "source build_dts.tcl; include_dtsi scratch_mem.dtsi"

dts_build:
	cd devicetree && gcc -I . -I my_dts -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp -o my_dts/system-top.dts.tmp my_dts/system-top.dts
	cd devicetree && dtc -I dts -O dtb -o ../devicetree.dtb my_dts/system-top.dts.tmp

uenv:
	test -f zc_pcie_dma.bit
	python -c 'import os; print "uenvcmd=fatload mmc 0 0x4000000 zc_pcie_dma.bit && fpga loadb 0 0x4000000 " + str(os.path.getsize("zc_pcie_dma.bit"))' > uEnv.txt

