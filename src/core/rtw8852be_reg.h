//
// rtw8852be_reg.h — RTL8852BE / AX PCIe Register Map Definitions
// Extracted from Linux rtw89 (lwfinger/rtw89 & drivers/net/wireless/realtek/rtw89)
// For macOS / freestanding C++17 driver environments
//
#pragma once

#include "linux_shim.h"

// ============================================================================
// 1. PCIe Configuration and Control Registers
// ============================================================================

// Mixed PCIe configuration (ASPM, L1 substates, clock control)
#define R_AX_PCIE_MIX_CFG_V1            0x300C
#define B_AX_ASPM_CTRL_L1               BIT(17)
#define B_AX_ASPM_CTRL_L0               BIT(16)
#define B_AX_ASPM_CTRL_MASK             GENMASK(17, 16)
#define B_AX_XFER_PENDING_FW            BIT(11)
#define B_AX_XFER_PENDING               BIT(10)
#define B_AX_REQ_EXIT_L1                BIT(9)
#define B_AX_REQ_ENTR_L1                BIT(8)
#define B_AX_L1SUB_DISABLE              BIT(0)

// PCIe / HAXI Initialization Config 1
#define R_AX_PCIE_INIT_CFG1             0x1000
#define R_AX_HAXI_INIT_CFG1             0x1000
#define B_AX_WD_ITVL_IDLE_V1_MASK       GENMASK(31, 28)
#define B_AX_WD_ITVL_ACT_V1_MASK        GENMASK(27, 24)
#define B_AX_DMA_MODE_MASK              GENMASK(19, 18)
#define DMA_MOD_PCIE_1B                 0x0
#define DMA_MOD_PCIE_4B                 0x1
#define DMA_MOD_USB                     0x2
#define DMA_MOD_SDIO                    0x3
#define B_AX_STOP_AXI_MST               BIT(17)
#define B_AX_HAXI_RST_KEEP_REG          BIT(16)
#define B_AX_RXHCI_EN                   BIT(15)
#define B_AX_RXHCI_EN_V1                BIT(15)
#define B_AX_RXBD_MODE                  BIT(14)
#define B_AX_RXBD_MODE_V1               BIT(14)
#define B_AX_PCIE_MAX_RXDMA_MASK        GENMASK(11, 8)
#define B_AX_HAXI_MAX_RXDMA_MASK        GENMASK(9, 8)
#define B_AX_TXHCI_EN                   BIT(7)
#define B_AX_TXHCI_EN_V1                BIT(7)
#define B_AX_DIS_RXDMA_PRE              BIT(6)
#define B_AX_TX_TRUNC_MODE              BIT(5)
#define B_AX_RX_TRUNC_MODE              BIT(4)
#define B_AX_FLUSH_AXI_MST              BIT(4)
#define B_AX_RST_BDRAM                  BIT(3)
#define B_AX_PCIE_MAX_TXDMA_MASK        GENMASK(2, 0)
#define B_AX_HAXI_MAX_TXDMA_MASK        GENMASK(1, 0)
#define B_AX_PCIE_PERST_KEEP_REG        BIT(1)
#define B_AX_PCIE_TRAIN_KEEP_REG        BIT(0)

// PCIe / HAXI Initialization Config 2
#define R_AX_PCIE_INIT_CFG2             0x1004
#define R_AX_HAXI_INIT_CFG2             0x1004
#define B_AX_WD_ITVL_IDLE               GENMASK(27, 24)
#define B_AX_WD_ITVL_ACT                GENMASK(19, 16)
#define B_AX_PCIE_RX_APPLEN_MASK        GENMASK(13, 0)

// PCIe Express Control / Link Control
#define R_AX_PCIE_EXP_CTRL              0x13F0
#define R_AX_PCIE_EXP_LNK_CTRL          0x13F0
#define B_AX_EN_CHKDSC_NO_RX_STUCK      BIT(20)
#define B_AX_MAX_TAG_NUM                GENMASK(18, 16)
#define B_AX_SIC_EN_FORCE_CLKREQ        BIT(4)

#define R_AX_HAXI_EXP_CTRL              0x1204
#define B_AX_MAX_TAG_NUM_V1_MASK        GENMASK(2, 0)

// Standard PCIe Capability Link Control offsets (PCI-SIG spec)
#ifndef PCI_EXP_LNKCTL
#define PCI_EXP_LNKCTL                  0x0010
#endif
#ifndef PCI_EXP_LNKCTL_ASPM_L0S
#define PCI_EXP_LNKCTL_ASPM_L0S         0x0001
#endif
#ifndef PCI_EXP_LNKCTL_ASPM_L1
#define PCI_EXP_LNKCTL_ASPM_L1          0x0002
#endif
#ifndef PCI_EXP_LNKCTL_CLKREQ_EN
#define PCI_EXP_LNKCTL_CLKREQ_EN        0x0100
#endif

// Power state and clock control
#define R_AX_PCIE_PS_CTRL               0x1008
#define B_AX_L1OFF_PWR_OFF_EN           BIT(5)

#define R_AX_PCIE_PS_CTRL_V1            0x3008
#define B_AX_CMAC_EXIT_L1_EN            BIT(7)
#define B_AX_DMAC0_EXIT_L1_EN           BIT(6)
#define B_AX_SEL_XFER_PENDING           BIT(3)
#define B_AX_SEL_REQ_ENTR_L1            BIT(2)
#define B_AX_SEL_REQ_EXIT_L1            BIT(0)

#define R_AX_L1_CLK_CTRL                0x3010
#define B_AX_CLK_REQ_N                  BIT(1)

#define R_AX_PCIE_LAT_CTRL              0x3044
#define B_AX_CLK_REQ_SEL_OPT            BIT(1)
#define B_AX_CLK_REQ_SEL                BIT(0)

#define R_AX_PCIE_HRPWM                 0x10C0
#define R_AX_PCIE_HRPWM_V1              0x30C0
#define R_AX_PCIE_CRPWM                 0x30C4

// ============================================================================
// 2. Interrupt Registers (HIMR / HISR)
// ============================================================================

// Interrupt Mask and Status - Group 0 (Gen AX)
#define R_AX_PCIE_HIMR0                 0x10B0
#define R_AX_PCIE_HIMR00                0x10B0
#define R_AX_HAXI_HIMR00                0x10B0
#define R_AX_PCIE_HISR0                 0x10B4
#define R_AX_PCIE_HISR00                0x10B4
#define R_AX_HAXI_HISR00                0x10B4

// V1 Interrupt Mask and Status
#define R_AX_PCIE_HIMR00_V1             0x30B0
#define R_AX_PCIE_HISR00_V1             0x30B4

// Legacy / Direct HIMR/HISR
#define R_AX_HIMR0                      0x01A0
#define R_AX_HISR0                      0x01A4

// HIMR0 / HISR0 Bitfield definitions
#define B_AX_HC00ISR_IND_INT_EN         BIT(27)
#define B_AX_HD1ISR_IND_INT_EN          BIT(26)
#define B_AX_HD0ISR_IND_INT_EN          BIT(25)
#define B_AX_HS0ISR_IND_INT_EN          BIT(24)
#define B_AX_HS0ISR_IND_INT_EN_WKARND   BIT(23)
#define B_AX_WDT_TIMEOUT_INT_EN         BIT(22)
#define B_AX_HALT_C2H_INT_EN            BIT(21)
#define B_AX_RETRAIN_INT_EN             BIT(21)
#define B_AX_RPQBD_FULL_INT_EN          BIT(20)
#define B_AX_RDU_INT_EN                 BIT(19)
#define B_AX_RXDMA_STUCK_INT_EN         BIT(18)
#define B_AX_TXDMA_STUCK_INT_EN         BIT(17)
#define B_AX_PCIE_HOTRST_INT_EN         BIT(16)
#define B_AX_PCIE_FLR_INT_EN            BIT(15)
#define B_AX_PCIE_PERST_INT_EN          BIT(14)
#define B_AX_TXDMA_CH12_INT_EN          BIT(13)
#define B_AX_TXDMA_CH9_INT_EN           BIT(12)
#define B_AX_TXDMA_CH8_INT_EN           BIT(11)
#define B_AX_TXDMA_ACH7_INT_EN          BIT(10)
#define B_AX_TXDMA_ACH6_INT_EN          BIT(9)
#define B_AX_TXDMA_ACH5_INT_EN          BIT(8)
#define B_AX_TXDMA_ACH4_INT_EN          BIT(7)
#define B_AX_TXDMA_ACH3_INT_EN          BIT(6)
#define B_AX_TXDMA_ACH2_INT_EN          BIT(5)
#define B_AX_TXDMA_ACH1_INT_EN          BIT(4)
#define B_AX_TXDMA_ACH0_INT_EN          BIT(3)
#define B_AX_RPQDMA_INT_EN              BIT(2)
#define B_AX_RXP1DMA_INT_EN             BIT(1)
#define B_AX_RXDMA_INT_EN               BIT(0)

#define B_AX_HC00ISR_IND_INT            BIT(27)
#define B_AX_HD1ISR_IND_INT             BIT(26)
#define B_AX_HD0ISR_IND_INT             BIT(25)
#define B_AX_HS0ISR_IND_INT             BIT(24)
#define B_AX_RETRAIN_INT                BIT(21)
#define B_AX_RPQBD_FULL_INT             BIT(20)
#define B_AX_RDU_INT                    BIT(19)
#define B_AX_RXDMA_STUCK_INT            BIT(18)
#define B_AX_TXDMA_STUCK_INT            BIT(17)
#define B_AX_PCIE_HOTRST_INT            BIT(16)
#define B_AX_PCIE_FLR_INT               BIT(15)
#define B_AX_PCIE_PERST_INT             BIT(14)
#define B_AX_TXDMA_CH12_INT             BIT(13)
#define B_AX_TXDMA_CH9_INT              BIT(12)
#define B_AX_TXDMA_CH8_INT              BIT(11)
#define B_AX_TXDMA_ACH7_INT             BIT(10)
#define B_AX_TXDMA_ACH6_INT             BIT(9)
#define B_AX_TXDMA_ACH5_INT             BIT(8)
#define B_AX_TXDMA_ACH4_INT             BIT(7)
#define B_AX_TXDMA_ACH3_INT             BIT(6)
#define B_AX_TXDMA_ACH2_INT             BIT(5)
#define B_AX_TXDMA_ACH1_INT             BIT(4)
#define B_AX_TXDMA_ACH0_INT             BIT(3)
#define B_AX_RPQDMA_INT                 BIT(2)
#define B_AX_RXP1DMA_INT                BIT(1)
#define B_AX_RXDMA_INT                  BIT(0)

// Interrupt Mask and Status - Group 1
#define R_AX_PCIE_HIMR1                 0x13B0
#define R_AX_PCIE_HIMR10                0x13B0
#define R_AX_PCIE_HISR1                 0x13B4
#define R_AX_PCIE_HISR10                0x13B4

#define R_AX_HIMR1                      0x01A8
#define R_AX_HISR1                      0x01AC

#define B_AX_HC10ISR_IND_INT_EN         BIT(28)
#define B_AX_TXDMA_CH11_INT_EN          BIT(12)
#define B_AX_TXDMA_CH10_INT_EN          BIT(11)

#define B_AX_HC10ISR_IND_INT            BIT(28)
#define B_AX_TXDMA_CH11_INT             BIT(12)
#define B_AX_TXDMA_CH10_INT             BIT(11)

// V1 specific interrupt bits
#define B_AX_HCI_AXIDMA_INT_EN          BIT(29)
#define B_AX_HC00ISR_IND_INT_EN_V1      BIT(28)
#define B_AX_HD1ISR_IND_INT_EN_V1       BIT(27)
#define B_AX_HD0ISR_IND_INT_EN_V1       BIT(26)
#define B_AX_HS1ISR_IND_INT_EN          BIT(25)
#define B_AX_PCIE_DBG_STE_INT_EN        BIT(13)

#define B_AX_HCI_AXIDMA_INT             BIT(29)
#define B_AX_HC00ISR_IND_INT_V1         BIT(28)
#define B_AX_HD1ISR_IND_INT_V1          BIT(27)
#define B_AX_HD0ISR_IND_INT_V1          BIT(26)
#define B_AX_HS1ISR_IND_INT             BIT(25)
#define B_AX_PCIE_DBG_STE_INT           BIT(13)

// ============================================================================
// 3. H2C / C2H Mailbox Registers
// ============================================================================

#define R_AX_HALT_H2C_CTRL              0x0160
#define R_AX_HALT_H2C                   0x0168
#define B_AX_HALT_H2C_TRIGGER           BIT(0)

#define R_AX_HALT_C2H_CTRL              0x0164
#define R_AX_HALT_C2H                   0x016C

// Register-based H2C / C2H queue CSR and control
#define R_AX_H2CQ_CSR                   0x8160
#define R_AX_H2CQ_CSR_V1                0x7160
#define R_AX_H2CREG_CTRL                0x8160
#define R_AX_H2CREG_CTRL_V1             0x7160
#define B_AX_H2CREG_TRIGGER             BIT(0)

#define R_AX_C2H_CTRL                   0x0164
#define R_AX_C2HREG_CTRL                0x8164
#define R_AX_C2HREG_CTRL_V1             0x7164
#define B_AX_C2HREG_TRIGGER             BIT(0)
#define B_AX_C2H_INT_EN                 BIT(0)
#define B_AX_C2H_INT                    BIT(0)

// H2C / C2H register payload data registers
#define R_AX_H2CREG_DATA0               0x8140
#define R_AX_H2CREG_DATA1               0x8144
#define R_AX_H2CREG_DATA2               0x8148
#define R_AX_H2CREG_DATA3               0x814C

#define R_AX_H2CREG_DATA0_V1            0x7140
#define R_AX_H2CREG_DATA1_V1            0x7144
#define R_AX_H2CREG_DATA2_V1            0x7148
#define R_AX_H2CREG_DATA3_V1            0x714C

#define R_AX_C2HREG_DATA0               0x8150
#define R_AX_C2HREG_DATA1               0x8154
#define R_AX_C2HREG_DATA2               0x8158
#define R_AX_C2HREG_DATA3               0x815C

#define R_AX_C2HREG_DATA0_V1            0x7150
#define R_AX_C2HREG_DATA1_V1            0x7154
#define R_AX_C2HREG_DATA2_V1            0x7158
#define R_AX_C2HREG_DATA3_V1            0x715C

// User Defined Mailbox Queue Depth & Status
#define R_AX_UDM1                       0x01F4
#define B_AX_UDM1_HALMAC_C2H_ENQ_CNT_MASK   GENMASK(15, 12)
#define B_AX_UDM1_HALMAC_H2C_DEQ_CNT_MASK   GENMASK(11, 8)
#define B_AX_UDM1_WCPU_C2H_ENQ_CNT_MASK     GENMASK(7, 4)
#define B_AX_UDM1_WCPU_H2C_DEQ_CNT_MASK     GENMASK(3, 0)

// ============================================================================
// 4. Firmware Download & Status Registers
// ============================================================================

#define R_AX_FWDL_STATUS_V1             0x01E0
#define R_AX_WCPU_FW_CTRL               0x01E0
#define B_AX_IDMEM_SHARE_MODE_RECORD_MASK   GENMASK(31, 24)
#define B_AX_IDMEM_SHARE_MODE_RECORD_VALID  BIT(16)
#define B_AX_WCPU_FWDL_STS_MASK         GENMASK(7, 5)
#define B_AX_FWDL_PATH_RDY              BIT(2)
#define B_AX_H2C_PATH_RDY               BIT(1)
#define B_AX_WCPU_FWDL_EN               BIT(0)

#define R_AX_SYS_STATUS                 0x00F4
#define R_AX_SYS_STATUS1                0x00F4
#define B_AX_SEL_0XC0_MASK              GENMASK(17, 16)
#define B_AX_AUTO_WLPON                 BIT(7)
#define B_AX_PAD_HCI_SEL_V2_MASK        GENMASK(5, 3)

#define R_AX_BOOT_DBG                   0x83F0

// Firmware download status code enum (matching Linux rtw89 rtw89_fwdl_check_type)
enum rtw89_fwdl_status_code {
    RTW89_FWDL_WCPU_FW_INIT_RDY = 7,
    RTW89_FWDL_CHECKSUM_FAIL    = 1,
    RTW89_FWDL_SECURITY_FAIL    = 2,
    RTW89_FWDL_CV_NOT_MATCH     = 3,
};

// ============================================================================
// 5. DMA Ring Descriptors and Index Registers
// ============================================================================

// TX Ring Descriptors / Index Registers (CH0..CH12)
#define R_AX_TXBD_IDX_CH0               0x1058
#define R_AX_TXBD_IDX_CH1               0x105C
#define R_AX_TXBD_IDX_CH2               0x1060
#define R_AX_TXBD_IDX_CH3               0x1064
#define R_AX_TXBD_IDX_CH4               0x1068
#define R_AX_TXBD_IDX_CH5               0x106C
#define R_AX_TXBD_IDX_CH6               0x1070
#define R_AX_TXBD_IDX_CH7               0x1074
#define R_AX_TXBD_IDX_CH8               0x1078  // Management Queue Band 0
#define R_AX_TXBD_IDX_CH9               0x107C  // High Priority Queue Band 0
#define R_AX_TXBD_IDX_CH10              0x137C  // Management Queue Band 1
#define R_AX_TXBD_IDX_CH11              0x1380  // High Priority Queue Band 1
#define R_AX_TXBD_IDX_CH12              0x1080  // FW Command Queue

#define R_AX_TXBD_IDX_CH10_V1           0x11D0
#define R_AX_TXBD_IDX_CH11_V1           0x11D4

// Linux rtw89 traditional AC / CH names
#define R_AX_ACH0_TXBD_IDX              0x1058
#define R_AX_ACH1_TXBD_IDX              0x105C
#define R_AX_ACH2_TXBD_IDX              0x1060
#define R_AX_ACH3_TXBD_IDX              0x1064
#define R_AX_ACH4_TXBD_IDX              0x1068
#define R_AX_ACH5_TXBD_IDX              0x106C
#define R_AX_ACH6_TXBD_IDX              0x1070
#define R_AX_ACH7_TXBD_IDX              0x1074
#define R_AX_CH8_TXBD_IDX               0x1078
#define R_AX_CH9_TXBD_IDX               0x107C
#define R_AX_CH10_TXBD_IDX              0x137C
#define R_AX_CH11_TXBD_IDX              0x1380
#define R_AX_CH12_TXBD_IDX              0x1080
#define R_AX_CH10_TXBD_IDX_V1           0x11D0
#define R_AX_CH11_TXBD_IDX_V1           0x11D4

// RX Ring Descriptors / Index Registers (CH0..CH1)
#define R_AX_RXBD_IDX_CH0               0x1050  // RXQ (Data Queue)
#define R_AX_RXBD_IDX_CH1               0x1054  // RPQ (Report Queue)
#define R_AX_RXQ_RXBD_IDX               0x1050
#define R_AX_RPQ_RXBD_IDX               0x1054

#define R_AX_RXBD_IDX_CH0_V1            0x1218
#define R_AX_RXBD_IDX_CH1_V1            0x121C
#define R_AX_RXQ_RXBD_IDX_V1            0x1218
#define R_AX_RPQ_RXBD_IDX_V1            0x121C

// Descriptor Read/Write Pointer Clear
#define R_AX_RXBD_RWPTR_CLR             0x1200
#define R_AX_RXBD_RWPTR_CLR_V1          0x1200
#define B_AX_CLR_RPQ_IDX                BIT(1)
#define B_AX_CLR_RXQ_IDX                BIT(0)

// DMA Stop & Busy Registers
#define R_AX_PCIE_DMA_STOP1             0x1010
#define R_AX_HAXI_DMA_STOP1             0x1010
#define B_AX_STOP_PCIEIO                BIT(20)
#define B_AX_STOP_WPDMA                 BIT(19)
#define B_AX_STOP_CH12                  BIT(18)
#define B_AX_STOP_CH9                   BIT(17)
#define B_AX_STOP_CH8                   BIT(16)
#define B_AX_STOP_ACH7                  BIT(15)
#define B_AX_STOP_ACH6                  BIT(14)
#define B_AX_STOP_ACH5                  BIT(13)
#define B_AX_STOP_ACH4                  BIT(12)
#define B_AX_STOP_ACH3                  BIT(11)
#define B_AX_STOP_ACH2                  BIT(10)
#define B_AX_STOP_ACH1                  BIT(9)
#define B_AX_STOP_ACH0                  BIT(8)
#define B_AX_TX_STOP1_MASK_V1           GENMASK(18, 8)

#define R_AX_HAXI_DMA_BUSY1             0x101C
#define B_AX_HAXIIO_BUSY                BIT(20)
#define B_AX_WPDMA_BUSY                 BIT(19)

// ============================================================================
// 6. MAC / BBP / RF Registers
// ============================================================================

// MAC Register Space Bounds
#define R_AX_MAC_REG                    0xC000
#define R_AX_CMAC_REG_START             0xC000
#define R_AX_CMAC_REG_END               0xFFFF
#define R_AX_WMAC_REG_START             0x8000
#define R_AX_WMAC_REG_END               0x8FFF
#define R_AX_DMAC_REG_START             0x9000
#define R_AX_DMAC_REG_END               0x9FFF

// System Clock Control
#define R_AX_SYS_CLK_CTRL               0x0008
#define B_AX_CPU_CLK_EN                 BIT(14)

// Analog Front End (AFE) Control
#define R_AX_AFE_CTRL                   0x0024
#define R_AX_AFE_CTRL1                  0x0024
#define B_AX_R_SYM_WLCMAC1_P4_PC_EN     BIT(4)
#define B_AX_R_SYM_WLCMAC1_P3_PC_EN     BIT(3)
#define B_AX_R_SYM_WLCMAC1_P2_PC_EN     BIT(2)
#define B_AX_R_SYM_WLCMAC1_P1_PC_EN     BIT(1)
#define B_AX_R_SYM_WLCMAC1_PC_EN        BIT(0)

#define R_AX_AFE_LDO_CTRL               0x0020
#define B_AX_AON_OFF_PC_EN              BIT(23)

// Reserved and System Config
#define R_AX_RSV_CTRL                   0x001C
#define B_AX_R_DIS_PRST                 BIT(6)
#define B_AX_WLOCK_1C_BIT6              BIT(5)

#define R_AX_SYS_CFG1                   0x00F0
#define B_AX_CHIP_VER_MASK              GENMASK(15, 12)

// MAC Address Registers
#define R_AX_MAC_ADDR_0                 0x0040
#define R_AX_MAC_ADDR_1                 0x0041
#define R_AX_MAC_ADDR_2                 0x0042
#define R_AX_MAC_ADDR_3                 0x0043
#define R_AX_MAC_ADDR_4                 0x0044
#define R_AX_MAC_ADDR_5                 0x0045
