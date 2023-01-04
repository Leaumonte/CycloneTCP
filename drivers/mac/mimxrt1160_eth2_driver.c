/**
 * @file mimxrt1160_eth2_driver.c
 * @brief NXP i.MX RT1160 Ethernet MAC driver (ENET_1G instance)
 *
 * @section License
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2010-2022 Oryx Embedded SARL. All rights reserved.
 *
 * This file is part of CycloneTCP Open.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * @author Oryx Embedded SARL (www.oryx-embedded.com)
 * @version 2.2.0
 **/

//Switch to the appropriate trace level
#define TRACE_LEVEL NIC_TRACE_LEVEL

//Dependencies
#include "fsl_device_registers.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "core/net.h"
#include "drivers/mac/mimxrt1160_eth2_driver.h"
#include "debug.h"

//Underlying network interface
static NetInterface *nicDriverInterface;

//IAR EWARM compiler?
#if defined(__ICCARM__)

//TX buffer
#pragma data_alignment = 64
#pragma location = MIMXRT1160_ETH2_RAM_SECTION
static uint8_t txBuffer[MIMXRT1160_ETH2_TX_BUFFER_COUNT][MIMXRT1160_ETH2_TX_BUFFER_SIZE];
//RX buffer
#pragma data_alignment = 64
#pragma location = MIMXRT1160_ETH2_RAM_SECTION
static uint8_t rxBuffer[MIMXRT1160_ETH2_RX_BUFFER_COUNT][MIMXRT1160_ETH2_RX_BUFFER_SIZE];
//TX buffer descriptors
#pragma data_alignment = 64
#pragma location = MIMXRT1160_ETH2_RAM_SECTION
static uint32_t txBufferDesc[MIMXRT1160_ETH2_TX_BUFFER_COUNT][8];
//RX buffer descriptors
#pragma data_alignment = 64
#pragma location = MIMXRT1160_ETH2_RAM_SECTION
static uint32_t rxBufferDesc[MIMXRT1160_ETH2_RX_BUFFER_COUNT][8];

//ARM or GCC compiler?
#else

//TX buffer
static uint8_t txBuffer[MIMXRT1160_ETH2_TX_BUFFER_COUNT][MIMXRT1160_ETH2_TX_BUFFER_SIZE]
   __attribute__((aligned(64), __section__(MIMXRT1160_ETH2_RAM_SECTION)));
//RX buffer
static uint8_t rxBuffer[MIMXRT1160_ETH2_RX_BUFFER_COUNT][MIMXRT1160_ETH2_RX_BUFFER_SIZE]
   __attribute__((aligned(64), __section__(MIMXRT1160_ETH2_RAM_SECTION)));
//TX buffer descriptors
static uint32_t txBufferDesc[MIMXRT1160_ETH2_TX_BUFFER_COUNT][8]
   __attribute__((aligned(64), __section__(MIMXRT1160_ETH2_RAM_SECTION)));
//RX buffer descriptors
static uint32_t rxBufferDesc[MIMXRT1160_ETH2_RX_BUFFER_COUNT][8]
   __attribute__((aligned(64), __section__(MIMXRT1160_ETH2_RAM_SECTION)));

#endif

//TX buffer index
static uint_t txBufferIndex;
//RX buffer index
static uint_t rxBufferIndex;


/**
 * @brief i.MX RT1160 Ethernet MAC driver (ENET_1G instance)
 **/

const NicDriver mimxrt1160Eth2Driver =
{
   NIC_TYPE_ETHERNET,
   ETH_MTU,
   mimxrt1160Eth2Init,
   mimxrt1160Eth2Tick,
   mimxrt1160Eth2EnableIrq,
   mimxrt1160Eth2DisableIrq,
   mimxrt1160Eth2EventHandler,
   mimxrt1160Eth2SendPacket,
   mimxrt1160Eth2UpdateMacAddrFilter,
   mimxrt1160Eth2UpdateMacConfig,
   mimxrt1160Eth2WritePhyReg,
   mimxrt1160Eth2ReadPhyReg,
   TRUE,
   TRUE,
   TRUE,
   FALSE
};


/**
 * @brief i.MX RT1160 Ethernet MAC initialization
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t mimxrt1160Eth2Init(NetInterface *interface)
{
   error_t error;
   uint32_t value;

   //Debug message
   TRACE_INFO("Initializing i.MX RT1160 Ethernet MAC (ENET_1G)...\r\n");

   //Save underlying network interface
   nicDriverInterface = interface;

   //Enable ENET_1G peripheral clock
   CLOCK_EnableClock(kCLOCK_Enet_1g);

   //GPIO configuration
   mimxrt1160Eth2InitGpio(interface);

   //Reset ENET_1G module
   ENET_1G->ECR = ENET_ECR_RESET_MASK;
   //Wait for the reset to complete
   while((ENET_1G->ECR & ENET_ECR_RESET_MASK) != 0)
   {
   }

   //Receive control register
   ENET_1G->RCR = ENET_RCR_MAX_FL(MIMXRT1160_ETH2_RX_BUFFER_SIZE) |
      ENET_RCR_RGMII_EN_MASK | ENET_RCR_MII_MODE_MASK;

   //Transmit control register
   ENET_1G->TCR = 0;
   //Configure MDC clock frequency
   ENET_1G->MSCR = ENET_MSCR_HOLDTIME(10) | ENET_MSCR_MII_SPEED(120);

   //Valid Ethernet PHY or switch driver?
   if(interface->phyDriver != NULL)
   {
      //Ethernet PHY initialization
      error = interface->phyDriver->init(interface);
   }
   else if(interface->switchDriver != NULL)
   {
      //Ethernet switch initialization
      error = interface->switchDriver->init(interface);
   }
   else
   {
      //The interface is not properly configured
      error = ERROR_FAILURE;
   }

   //Any error to report?
   if(error)
   {
      return error;
   }

   //Set the MAC address of the station (upper 16 bits)
   value = interface->macAddr.b[5];
   value |= (interface->macAddr.b[4] << 8);
   ENET_1G->PAUR = ENET_PAUR_PADDR2(value) | ENET_PAUR_TYPE(0x8808);

   //Set the MAC address of the station (lower 32 bits)
   value = interface->macAddr.b[3];
   value |= (interface->macAddr.b[2] << 8);
   value |= (interface->macAddr.b[1] << 16);
   value |= (interface->macAddr.b[0] << 24);
   ENET_1G->PALR = ENET_PALR_PADDR1(value);

   //Hash table for unicast address filtering
   ENET_1G->IALR = 0;
   ENET_1G->IAUR = 0;
   //Hash table for multicast address filtering
   ENET_1G->GALR = 0;
   ENET_1G->GAUR = 0;

   //Disable transmit accelerator functions
   ENET_1G->TACC = 0;
   //Disable receive accelerator functions
   ENET_1G->RACC = 0;

   //Use enhanced buffer descriptors
   ENET_1G->ECR = ENET_ECR_DBSWP_MASK | ENET_ECR_EN1588_MASK;

   //Reset statistics counters
   ENET_1G->MIBC = ENET_MIBC_MIB_CLEAR_MASK;
   ENET_1G->MIBC = 0;

   //Initialize buffer descriptors
   mimxrt1160Eth2InitBufferDesc(interface);

   //Clear any pending interrupts
   ENET_1G->EIR = 0xFFFFFFFF;
   //Enable desired interrupts
   ENET_1G->EIMR = ENET_EIMR_TXF_MASK | ENET_EIMR_RXF_MASK | ENET_EIMR_EBERR_MASK;

   //Set priority grouping (4 bits for pre-emption priority, no bits for subpriority)
   NVIC_SetPriorityGrouping(MIMXRT1160_ETH2_IRQ_PRIORITY_GROUPING);

   //Configure ENET_1G interrupt priority
   NVIC_SetPriority(ENET_1G_IRQn, NVIC_EncodePriority(MIMXRT1160_ETH2_IRQ_PRIORITY_GROUPING,
      MIMXRT1160_ETH2_IRQ_GROUP_PRIORITY, MIMXRT1160_ETH2_IRQ_SUB_PRIORITY));

   //Enable Ethernet MAC
   ENET_1G->ECR |= ENET_ECR_ETHEREN_MASK;
   //Instruct the DMA to poll the receive descriptor list
   ENET_1G->RDAR = ENET_RDAR_RDAR_MASK;

   //Accept any packets from the upper layer
   osSetEvent(&interface->nicTxEvent);

   //Successful initialization
   return NO_ERROR;
}


/**
 * @brief GPIO configuration
 * @param[in] interface Underlying network interface
 **/

__weak_func void mimxrt1160Eth2InitGpio(NetInterface *interface)
{
//MIMXRT1160-EVK evaluation board?
#if defined(USE_MIMXRT1160_EVK)
   gpio_pin_config_t pinConfig;
   clock_root_config_t rootConfig = {0};
#if 0
   clock_sys_pll1_config_t sysPll1Config = {0};

   //Initialize system PLL1
   sysPll1Config.pllDiv2En = true;
   CLOCK_InitSysPll1(&sysPll1Config);
#endif

   //Generate 125MHz root clock
   rootConfig.clockOff = false;
   rootConfig.mux = kCLOCK_ENET2_ClockRoot_MuxSysPll1Div2;
   rootConfig.div = 4;
   CLOCK_SetRootClock(kCLOCK_Root_Enet2, &rootConfig);

#if 0
   //Initialize PLL PFD3 (528*18/24 = 396MHz)
   CLOCK_InitPfd(kCLOCK_PllSys2, kCLOCK_Pfd3, 24);

   //Generate 198MHz bus clock
   rootConfig.clockOff = false;
   rootConfig.mux = kCLOCK_BUS_ClockRoot_MuxSysPll2Pfd3;
   rootConfig.div = 2;
   CLOCK_SetRootClock(kCLOCK_Root_Bus, &rootConfig);
#endif

   //ENET_1G_TX_CLK is driven by ENET2_CLK_ROOT
   IOMUXC_GPR->GPR5 &= ~IOMUXC_GPR_GPR5_ENET1G_TX_CLK_SEL_MASK;
   //Enable ENET_1G_TX_CLK output
   IOMUXC_GPR->GPR5 |= IOMUXC_GPR_GPR5_ENET1G_RGMII_EN_MASK;

   //Enable IOMUXC clock
   CLOCK_EnableClock(kCLOCK_Iomuxc);

   //Configure GPIO_DISP_B1_00 pin as ENET_1G_RX_EN
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_00_ENET_1G_RX_EN, 0);

   //Set GPIO_DISP_B1_00 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_00_ENET_1G_RX_EN,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(2) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_01 pin as ENET_1G_RX_CLK
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_01_ENET_1G_RX_CLK, 0);

   //Set GPIO_DISP_B1_01 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_01_ENET_1G_RX_CLK,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(2) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_02 pin as ENET_1G_RX_DATA00
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_02_ENET_1G_RX_DATA00, 0);

   //Set GPIO_DISP_B1_02 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_02_ENET_1G_RX_DATA00,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(2) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_03 pin as ENET_1G_RX_DATA01
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_03_ENET_1G_RX_DATA01, 0);

   //Set GPIO_DISP_B1_03 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_03_ENET_1G_RX_DATA01,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(2) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_04 pin as ENET_1G_RX_DATA02
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_04_ENET_1G_RX_DATA02, 0);

   //Set GPIO_DISP_B1_04 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_04_ENET_1G_RX_DATA02,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(2) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_05 pin as ENET_1G_RX_DATA03
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_05_ENET_1G_RX_DATA03, 0);

   //Set GPIO_DISP_B1_05 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_05_ENET_1G_RX_DATA03,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(2) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_06 pin as ENET_1G_TX_DATA03
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_06_ENET_1G_TX_DATA03, 0);

   //Set GPIO_DISP_B1_06 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_06_ENET_1G_TX_DATA03,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(3) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_07 pin as ENET_1G_TX_DATA02
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_07_ENET_1G_TX_DATA02, 0);

   //Set GPIO_DISP_B1_07 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_07_ENET_1G_TX_DATA02,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(3) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_08 pin as ENET_1G_TX_DATA01
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_08_ENET_1G_TX_DATA01, 0);

   //Set GPIO_DISP_B1_08 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_08_ENET_1G_TX_DATA01,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(3) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_09 pin as ENET_1G_TX_DATA00
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_09_ENET_1G_TX_DATA00, 0);

   //Set GPIO_DISP_B1_09 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_09_ENET_1G_TX_DATA00,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(3) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_10 pin as ENET_1G_TX_EN
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_10_ENET_1G_TX_EN, 0);

   //Set GPIO_DISP_B1_10 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_10_ENET_1G_TX_EN,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(3) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B1_11 pin as ENET_1G_TX_CLK
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B1_11_ENET_1G_TX_CLK_IO, 0);

   //Set GPIO_DISP_B1_11 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B1_11_ENET_1G_TX_CLK_IO,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(3) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_EMC_B2_19 pin as ENET_1G_MDC
   IOMUXC_SetPinMux(IOMUXC_GPIO_EMC_B2_19_ENET_1G_MDC, 0);

   //Set GPIO_EMC_B2_19 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_EMC_B2_19_ENET_1G_MDC,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(3) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_EMC_B2_20 pin as ENET_1G_MDIO
   IOMUXC_SetPinMux(IOMUXC_GPIO_EMC_B2_20_ENET_1G_MDIO, 0);

   //Set GPIO_EMC_B2_20 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_EMC_B2_20_ENET_1G_MDIO,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PULL(1) |
      IOMUXC_SW_PAD_CTL_PAD_PDRV(0));

   //Configure GPIO_DISP_B2_13 pin as GPIO11_IO14
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B2_13_GPIO11_IO14, 0);

   //Set GPIO_DISP_B2_13 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B2_13_GPIO11_IO14,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PUS(0) |
      IOMUXC_SW_PAD_CTL_PAD_PUE(0) |
      IOMUXC_SW_PAD_CTL_PAD_DSE(1) |
      IOMUXC_SW_PAD_CTL_PAD_SRE(0));

   //Configure GPIO_DISP_B2_12 pin as GPIO11_IO13
   IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B2_12_GPIO11_IO13, 0);

   //Set GPIO_DISP_B2_12 pad properties
   IOMUXC_SetPinConfig(IOMUXC_GPIO_DISP_B2_12_GPIO11_IO13,
      IOMUXC_SW_PAD_CTL_PAD_DWP_LOCK(0) |
      IOMUXC_SW_PAD_CTL_PAD_DWP(0) |
      IOMUXC_SW_PAD_CTL_PAD_ODE(0) |
      IOMUXC_SW_PAD_CTL_PAD_PUS(0) |
      IOMUXC_SW_PAD_CTL_PAD_PUE(0) |
      IOMUXC_SW_PAD_CTL_PAD_DSE(1) |
      IOMUXC_SW_PAD_CTL_PAD_SRE(0));

   //Configure ENET_1G_RST as an output
   pinConfig.direction = kGPIO_DigitalOutput;
   pinConfig.outputLogic = 0;
   pinConfig.interruptMode = kGPIO_NoIntmode;
   GPIO_PinInit(GPIO11, 14, &pinConfig);

   //Configure ENET_1G_INT as an input
   pinConfig.direction = kGPIO_DigitalInput;
   pinConfig.outputLogic = 0;
   pinConfig.interruptMode = kGPIO_NoIntmode;
   GPIO_PinInit(GPIO11, 13, &pinConfig);

   //Reset PHY transceiver (hard reset)
   GPIO_PinWrite(GPIO11, 14, 0);
   sleep(10);
   GPIO_PinWrite(GPIO11, 14, 1);
   sleep(10);
#endif
}


/**
 * @brief Initialize buffer descriptors
 * @param[in] interface Underlying network interface
 **/

void mimxrt1160Eth2InitBufferDesc(NetInterface *interface)
{
   uint_t i;
   uint32_t address;

   //Clear TX and RX buffer descriptors
   osMemset(txBufferDesc, 0, sizeof(txBufferDesc));
   osMemset(rxBufferDesc, 0, sizeof(rxBufferDesc));

   //Initialize TX buffer descriptors
   for(i = 0; i < MIMXRT1160_ETH2_TX_BUFFER_COUNT; i++)
   {
      //Calculate the address of the current TX buffer
      address = (uint32_t) txBuffer[i];
      //Transmit buffer address
      txBufferDesc[i][1] = address;
      //Generate interrupts
      txBufferDesc[i][2] = ENET_TBD2_INT;
   }

   //Mark the last descriptor entry with the wrap flag
   txBufferDesc[i - 1][0] |= ENET_TBD0_W;
   //Initialize TX buffer index
   txBufferIndex = 0;

   //Initialize RX buffer descriptors
   for(i = 0; i < MIMXRT1160_ETH2_RX_BUFFER_COUNT; i++)
   {
      //Calculate the address of the current RX buffer
      address = (uint32_t) rxBuffer[i];
      //The descriptor is initially owned by the DMA
      rxBufferDesc[i][0] = ENET_RBD0_E;
      //Receive buffer address
      rxBufferDesc[i][1] = address;
      //Generate interrupts
      rxBufferDesc[i][2] = ENET_RBD2_INT;
   }

   //Mark the last descriptor entry with the wrap flag
   rxBufferDesc[i - 1][0] |= ENET_RBD0_W;
   //Initialize RX buffer index
   rxBufferIndex = 0;

   //Start location of the TX descriptor list
   ENET_1G->TDSR = (uint32_t) txBufferDesc;
   //Start location of the RX descriptor list
   ENET_1G->RDSR = (uint32_t) rxBufferDesc;
   //Maximum receive buffer size
   ENET_1G->MRBR = MIMXRT1160_ETH2_RX_BUFFER_SIZE;
}


/**
 * @brief i.MX RT1160 Ethernet MAC timer handler
 *
 * This routine is periodically called by the TCP/IP stack to handle periodic
 * operations such as polling the link state
 *
 * @param[in] interface Underlying network interface
 **/

void mimxrt1160Eth2Tick(NetInterface *interface)
{
   //Valid Ethernet PHY or switch driver?
   if(interface->phyDriver != NULL)
   {
      //Handle periodic operations
      interface->phyDriver->tick(interface);
   }
   else if(interface->switchDriver != NULL)
   {
      //Handle periodic operations
      interface->switchDriver->tick(interface);
   }
   else
   {
      //Just for sanity
   }
}


/**
 * @brief Enable interrupts
 * @param[in] interface Underlying network interface
 **/

void mimxrt1160Eth2EnableIrq(NetInterface *interface)
{
   //Enable Ethernet MAC interrupts
   NVIC_EnableIRQ(ENET_1G_IRQn);

   //Valid Ethernet PHY or switch driver?
   if(interface->phyDriver != NULL)
   {
      //Enable Ethernet PHY interrupts
      interface->phyDriver->enableIrq(interface);
   }
   else if(interface->switchDriver != NULL)
   {
      //Enable Ethernet switch interrupts
      interface->switchDriver->enableIrq(interface);
   }
   else
   {
      //Just for sanity
   }
}


/**
 * @brief Disable interrupts
 * @param[in] interface Underlying network interface
 **/

void mimxrt1160Eth2DisableIrq(NetInterface *interface)
{
   //Disable Ethernet MAC interrupts
   NVIC_DisableIRQ(ENET_1G_IRQn);

   //Valid Ethernet PHY or switch driver?
   if(interface->phyDriver != NULL)
   {
      //Disable Ethernet PHY interrupts
      interface->phyDriver->disableIrq(interface);
   }
   else if(interface->switchDriver != NULL)
   {
      //Disable Ethernet switch interrupts
      interface->switchDriver->disableIrq(interface);
   }
   else
   {
      //Just for sanity
   }
}


/**
 * @brief Ethernet MAC interrupt
 **/

void ENET_1G_IRQHandler(void)
{
   bool_t flag;
   uint32_t events;

   //Interrupt service routine prologue
   osEnterIsr();

   //This flag will be set if a higher priority task must be woken
   flag = FALSE;
   //Read interrupt event register
   events = ENET_1G->EIR;

   //Packet transmitted?
   if((events & ENET_EIR_TXF_MASK) != 0)
   {
      //Clear TXF interrupt flag
      ENET_1G->EIR = ENET_EIR_TXF_MASK;

      //Check whether the TX buffer is available for writing
      if((txBufferDesc[txBufferIndex][0] & ENET_TBD0_R) == 0)
      {
         //Notify the TCP/IP stack that the transmitter is ready to send
         flag = osSetEventFromIsr(&nicDriverInterface->nicTxEvent);
      }

      //Instruct the DMA to poll the transmit descriptor list
      ENET_1G->TDAR = ENET_TDAR_TDAR_MASK;
   }

   //Packet received?
   if((events & ENET_EIR_RXF_MASK) != 0)
   {
      //Disable RXF interrupt
      ENET_1G->EIMR &= ~ENET_EIMR_RXF_MASK;

      //Set event flag
      nicDriverInterface->nicEvent = TRUE;
      //Notify the TCP/IP stack of the event
      flag = osSetEventFromIsr(&netEvent);
   }

   //System bus error?
   if((events & ENET_EIR_EBERR_MASK) != 0)
   {
      //Disable EBERR interrupt
      ENET_1G->EIMR &= ~ENET_EIMR_EBERR_MASK;

      //Set event flag
      nicDriverInterface->nicEvent = TRUE;
      //Notify the TCP/IP stack of the event
      flag |= osSetEventFromIsr(&netEvent);
   }

   //Interrupt service routine epilogue
   osExitIsr(flag);
}


/**
 * @brief i.MX RT1160 Ethernet MAC event handler
 * @param[in] interface Underlying network interface
 **/

void mimxrt1160Eth2EventHandler(NetInterface *interface)
{
   error_t error;
   uint32_t status;

   //Read interrupt event register
   status = ENET_1G->EIR;

   //Packet received?
   if((status & ENET_EIR_RXF_MASK) != 0)
   {
      //Clear RXF interrupt flag
      ENET_1G->EIR = ENET_EIR_RXF_MASK;

      //Process all pending packets
      do
      {
         //Read incoming packet
         error = mimxrt1160Eth2ReceivePacket(interface);

         //No more data in the receive buffer?
      } while(error != ERROR_BUFFER_EMPTY);
   }

   //System bus error?
   if((status & ENET_EIR_EBERR_MASK) != 0)
   {
      //Clear EBERR interrupt flag
      ENET_1G->EIR = ENET_EIR_EBERR_MASK;

      //Disable Ethernet MAC
      ENET_1G->ECR &= ~ENET_ECR_ETHEREN_MASK;
      //Reset buffer descriptors
      mimxrt1160Eth2InitBufferDesc(interface);
      //Resume normal operation
      ENET_1G->ECR |= ENET_ECR_ETHEREN_MASK;
      //Instruct the DMA to poll the receive descriptor list
      ENET_1G->RDAR = ENET_RDAR_RDAR_MASK;
   }

   //Re-enable Ethernet MAC interrupts
   ENET_1G->EIMR = ENET_EIMR_TXF_MASK | ENET_EIMR_RXF_MASK | ENET_EIMR_EBERR_MASK;
}


/**
 * @brief Send a packet
 * @param[in] interface Underlying network interface
 * @param[in] buffer Multi-part buffer containing the data to send
 * @param[in] offset Offset to the first data byte
 * @param[in] ancillary Additional options passed to the stack along with
 *   the packet
 * @return Error code
 **/

error_t mimxrt1160Eth2SendPacket(NetInterface *interface,
   const NetBuffer *buffer, size_t offset, NetTxAncillary *ancillary)
{
   static uint8_t temp[MIMXRT1160_ETH2_TX_BUFFER_SIZE];
   size_t length;

   //Retrieve the length of the packet
   length = netBufferGetLength(buffer) - offset;

   //Check the frame length
   if(length > MIMXRT1160_ETH2_TX_BUFFER_SIZE)
   {
      //The transmitter can accept another packet
      osSetEvent(&interface->nicTxEvent);
      //Report an error
      return ERROR_INVALID_LENGTH;
   }

   //Make sure the current buffer is available for writing
   if((txBufferDesc[txBufferIndex][0] & ENET_TBD0_R) != 0)
   {
      return ERROR_FAILURE;
   }

   //Copy user data to the transmit buffer
   netBufferRead(temp, buffer, offset, length);
   osMemcpy(txBuffer[txBufferIndex], temp, (length + 3) & ~3UL);

   //Clear BDU flag
   txBufferDesc[txBufferIndex][4] = 0;

   //Check current index
   if(txBufferIndex < (MIMXRT1160_ETH2_TX_BUFFER_COUNT - 1))
   {
      //Give the ownership of the descriptor to the DMA engine
      txBufferDesc[txBufferIndex][0] = ENET_TBD0_R | ENET_TBD0_L |
         ENET_TBD0_TC | (length & ENET_TBD0_DATA_LENGTH);

      //Point to the next buffer
      txBufferIndex++;
   }
   else
   {
      //Give the ownership of the descriptor to the DMA engine
      txBufferDesc[txBufferIndex][0] = ENET_TBD0_R | ENET_TBD0_W |
         ENET_TBD0_L | ENET_TBD0_TC | (length & ENET_TBD0_DATA_LENGTH);

      //Wrap around
      txBufferIndex = 0;
   }

   //Data synchronization barrier
   __DSB();

   //Instruct the DMA to poll the transmit descriptor list
   ENET_1G->TDAR = ENET_TDAR_TDAR_MASK;

   //Check whether the next buffer is available for writing
   if((txBufferDesc[txBufferIndex][0] & ENET_TBD0_R) == 0)
   {
      //The transmitter can accept another packet
      osSetEvent(&interface->nicTxEvent);
   }

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Receive a packet
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t mimxrt1160Eth2ReceivePacket(NetInterface *interface)
{
   static uint8_t temp[MIMXRT1160_ETH2_RX_BUFFER_SIZE];
   error_t error;
   size_t n;
   NetRxAncillary ancillary;

   //Current buffer available for reading?
   if((rxBufferDesc[rxBufferIndex][0] & ENET_RBD0_E) == 0)
   {
      //The frame should not span multiple buffers
      if((rxBufferDesc[rxBufferIndex][0] & ENET_RBD0_L) != 0)
      {
         //Check whether an error occurred
         if((rxBufferDesc[rxBufferIndex][0] & (ENET_RBD0_LG | ENET_RBD0_NO |
            ENET_RBD0_CR | ENET_RBD0_OV | ENET_RBD0_TR)) == 0)
         {
            //Retrieve the length of the frame
            n = rxBufferDesc[rxBufferIndex][0] & ENET_RBD0_DATA_LENGTH;
            //Limit the number of data to read
            n = MIN(n, MIMXRT1160_ETH2_RX_BUFFER_SIZE);

            //Copy data from the receive buffer
            osMemcpy(temp, rxBuffer[rxBufferIndex], (n + 3) & ~3UL);

            //Additional options can be passed to the stack along with the packet
            ancillary = NET_DEFAULT_RX_ANCILLARY;

            //Pass the packet to the upper layer
            nicProcessPacket(interface, temp, n, &ancillary);

            //Valid packet received
            error = NO_ERROR;
         }
         else
         {
            //The received packet contains an error
            error = ERROR_INVALID_PACKET;
         }
      }
      else
      {
         //The packet is not valid
         error = ERROR_INVALID_PACKET;
      }

      //Clear BDU flag
      rxBufferDesc[rxBufferIndex][4] = 0;

      //Check current index
      if(rxBufferIndex < (MIMXRT1160_ETH2_RX_BUFFER_COUNT - 1))
      {
         //Give the ownership of the descriptor back to the DMA engine
         rxBufferDesc[rxBufferIndex][0] = ENET_RBD0_E;
         //Point to the next buffer
         rxBufferIndex++;
      }
      else
      {
         //Give the ownership of the descriptor back to the DMA engine
         rxBufferDesc[rxBufferIndex][0] = ENET_RBD0_E | ENET_RBD0_W;
         //Wrap around
         rxBufferIndex = 0;
      }

      //Instruct the DMA to poll the receive descriptor list
      ENET_1G->RDAR = ENET_RDAR_RDAR_MASK;
   }
   else
   {
      //No more data in the receive buffer
      error = ERROR_BUFFER_EMPTY;
   }

   //Return status code
   return error;
}


/**
 * @brief Configure MAC address filtering
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t mimxrt1160Eth2UpdateMacAddrFilter(NetInterface *interface)
{
   uint_t i;
   uint_t k;
   uint32_t crc;
   uint32_t value;
   uint32_t unicastHashTable[2];
   uint32_t multicastHashTable[2];
   MacFilterEntry *entry;

   //Debug message
   TRACE_DEBUG("Updating MAC filter...\r\n");

   //Set the MAC address of the station (upper 16 bits)
   value = interface->macAddr.b[5];
   value |= (interface->macAddr.b[4] << 8);
   ENET_1G->PAUR = ENET_PAUR_PADDR2(value) | ENET_PAUR_TYPE(0x8808);

   //Set the MAC address of the station (lower 32 bits)
   value = interface->macAddr.b[3];
   value |= (interface->macAddr.b[2] << 8);
   value |= (interface->macAddr.b[1] << 16);
   value |= (interface->macAddr.b[0] << 24);
   ENET_1G->PALR = ENET_PALR_PADDR1(value);

   //Clear hash table (unicast address filtering)
   unicastHashTable[0] = 0;
   unicastHashTable[1] = 0;

   //Clear hash table (multicast address filtering)
   multicastHashTable[0] = 0;
   multicastHashTable[1] = 0;

   //The MAC address filter contains the list of MAC addresses to accept
   //when receiving an Ethernet frame
   for(i = 0; i < MAC_ADDR_FILTER_SIZE; i++)
   {
      //Point to the current entry
      entry = &interface->macAddrFilter[i];

      //Valid entry?
      if(entry->refCount > 0)
      {
         //Compute CRC over the current MAC address
         crc = mimxrt1160Eth2CalcCrc(&entry->addr, sizeof(MacAddr));

         //The upper 6 bits in the CRC register are used to index the
         //contents of the hash table
         k = (crc >> 26) & 0x3F;

         //Multicast address?
         if(macIsMulticastAddr(&entry->addr))
         {
            //Update the multicast hash table
            multicastHashTable[k / 32] |= (1 << (k % 32));
         }
         else
         {
            //Update the unicast hash table
            unicastHashTable[k / 32] |= (1 << (k % 32));
         }
      }
   }

   //Write the hash table (unicast address filtering)
   ENET_1G->IALR = unicastHashTable[0];
   ENET_1G->IAUR = unicastHashTable[1];

   //Write the hash table (multicast address filtering)
   ENET_1G->GALR = multicastHashTable[0];
   ENET_1G->GAUR = multicastHashTable[1];

   //Debug message
   TRACE_DEBUG("  IALR = %08" PRIX32 "\r\n", ENET_1G->IALR);
   TRACE_DEBUG("  IAUR = %08" PRIX32 "\r\n", ENET_1G->IAUR);
   TRACE_DEBUG("  GALR = %08" PRIX32 "\r\n", ENET_1G->GALR);
   TRACE_DEBUG("  GAUR = %08" PRIX32 "\r\n", ENET_1G->GAUR);

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Adjust MAC configuration parameters for proper operation
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t mimxrt1160Eth2UpdateMacConfig(NetInterface *interface)
{
   //Disable Ethernet MAC while modifying configuration registers
   ENET_1G->ECR &= ~ENET_ECR_ETHEREN_MASK;

   //1000BASE-T operation mode?
   if(interface->linkSpeed == NIC_LINK_SPEED_1GBPS)
   {
      ENET_1G->ECR |= ENET_ECR_SPEED_MASK;
      ENET_1G->RCR &= ~ENET_RCR_RMII_10T_MASK;
   }
   //100BASE-TX operation mode?
   else if(interface->linkSpeed == NIC_LINK_SPEED_100MBPS)
   {
      ENET_1G->ECR &= ~ENET_ECR_SPEED_MASK;
      ENET_1G->RCR &= ~ENET_RCR_RMII_10T_MASK;
   }
   //10BASE-T operation mode?
   else
   {
      ENET_1G->ECR &= ~ENET_ECR_SPEED_MASK;
      ENET_1G->RCR |= ENET_RCR_RMII_10T_MASK;
   }

   //Half-duplex or full-duplex mode?
   if(interface->duplexMode == NIC_FULL_DUPLEX_MODE)
   {
      //Full-duplex mode
      ENET_1G->TCR |= ENET_TCR_FDEN_MASK;
      //Receive path operates independently of transmit
      ENET_1G->RCR &= ~ENET_RCR_DRT_MASK;
   }
   else
   {
      //Half-duplex mode
      ENET_1G->TCR &= ~ENET_TCR_FDEN_MASK;
      //Disable reception of frames while transmitting
      ENET_1G->RCR |= ENET_RCR_DRT_MASK;
   }

   //Reset buffer descriptors
   mimxrt1160Eth2InitBufferDesc(interface);

   //Re-enable Ethernet MAC
   ENET_1G->ECR |= ENET_ECR_ETHEREN_MASK;
   //Instruct the DMA to poll the receive descriptor list
   ENET_1G->RDAR = ENET_RDAR_RDAR_MASK;

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Write PHY register
 * @param[in] opcode Access type (2 bits)
 * @param[in] phyAddr PHY address (5 bits)
 * @param[in] regAddr Register address (5 bits)
 * @param[in] data Register value
 **/

void mimxrt1160Eth2WritePhyReg(uint8_t opcode, uint8_t phyAddr,
   uint8_t regAddr, uint16_t data)
{
   uint32_t temp;

   //Valid opcode?
   if(opcode == SMI_OPCODE_WRITE)
   {
      //Set up a write operation
      temp = ENET_MMFR_ST(1) | ENET_MMFR_OP(1) | ENET_MMFR_TA(2);
      //PHY address
      temp |= ENET_MMFR_PA(phyAddr);
      //Register address
      temp |= ENET_MMFR_RA(regAddr);
      //Register value
      temp |= ENET_MMFR_DATA(data);

      //Clear MII interrupt flag
      ENET_1G->EIR = ENET_EIR_MII_MASK;
      //Start a write operation
      ENET_1G->MMFR = temp;

      //Wait for the write to complete
      while((ENET_1G->EIR & ENET_EIR_MII_MASK) == 0)
      {
      }
   }
   else
   {
      //The MAC peripheral only supports standard Clause 22 opcodes
   }
}


/**
 * @brief Read PHY register
 * @param[in] opcode Access type (2 bits)
 * @param[in] phyAddr PHY address (5 bits)
 * @param[in] regAddr Register address (5 bits)
 * @return Register value
 **/

uint16_t mimxrt1160Eth2ReadPhyReg(uint8_t opcode, uint8_t phyAddr,
   uint8_t regAddr)
{
   uint16_t data;
   uint32_t temp;

   //Valid opcode?
   if(opcode == SMI_OPCODE_READ)
   {
      //Set up a read operation
      temp = ENET_MMFR_ST(1) | ENET_MMFR_OP(2) | ENET_MMFR_TA(2);
      //PHY address
      temp |= ENET_MMFR_PA(phyAddr);
      //Register address
      temp |= ENET_MMFR_RA(regAddr);

      //Clear MII interrupt flag
      ENET_1G->EIR = ENET_EIR_MII_MASK;
      //Start a read operation
      ENET_1G->MMFR = temp;

      //Wait for the read to complete
      while((ENET_1G->EIR & ENET_EIR_MII_MASK) == 0)
      {
      }

      //Get register value
      data = ENET_1G->MMFR & ENET_MMFR_DATA_MASK;
   }
   else
   {
      //The MAC peripheral only supports standard Clause 22 opcodes
      data = 0;
   }

   //Return the value of the PHY register
   return data;
}


/**
 * @brief CRC calculation
 * @param[in] data Pointer to the data over which to calculate the CRC
 * @param[in] length Number of bytes to process
 * @return Resulting CRC value
 **/

uint32_t mimxrt1160Eth2CalcCrc(const void *data, size_t length)
{
   uint_t i;
   uint_t j;
   uint32_t crc;
   const uint8_t *p;

   //Point to the data over which to calculate the CRC
   p = (uint8_t *) data;
   //CRC preset value
   crc = 0xFFFFFFFF;

   //Loop through data
   for(i = 0; i < length; i++)
   {
      //Update CRC value
      crc ^= p[i];

      //The message is processed bit by bit
      for(j = 0; j < 8; j++)
      {
         if((crc & 0x01) != 0)
         {
            crc = (crc >> 1) ^ 0xEDB88320;
         }
         else
         {
            crc = crc >> 1;
         }
      }
   }

   //Return CRC value
   return crc;
}
