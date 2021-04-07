ZC_DMA_MEM_VERSION = 1.0
ZC_DMA_MEM_SITE = $(BR2_EXTERNAL)
ZC_DMA_MEM_SITE_METHOD = local
$(eval $(kernel-module))
$(eval $(generic-package))

