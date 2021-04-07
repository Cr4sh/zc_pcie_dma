#ifndef _TLP_H_
#define _TLP_H_

// PCI-E TLP size
#define TLP_3_NO_DATA    0
#define TLP_4_NO_DATA    1
#define TLP_3_DATA       2
#define TLP_4_DATA       3

// PCI-E TLP type
#define TLP_TYPE_MRd32   0x00
#define TLP_TYPE_MRd64   0x20
#define TLP_TYPE_MRdLk32 0x01
#define TLP_TYPE_MRdLk64 0x21
#define TLP_TYPE_MWr32   0x40
#define TLP_TYPE_MWr64   0x60
#define TLP_TYPE_IORd    0x02
#define TLP_TYPE_IOWr    0x42
#define TLP_TYPE_CfgRd0  0x04
#define TLP_TYPE_CfgRd1  0x05
#define TLP_TYPE_CfgWr0  0x44
#define TLP_TYPE_CfgWr1  0x45
#define TLP_TYPE_Cpl     0x0A
#define TLP_TYPE_CplD    0x4A
#define TLP_TYPE_CplLk   0x0B
#define TLP_TYPE_CplLkD  0x4B

#endif
