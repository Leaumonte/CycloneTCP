/**
 * @file samv71_eth_driver.c
 * @brief SAMV71 Ethernet MAC driver
 *
 * @section License
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2010-2023 Oryx Embedded SARL. All rights reserved.
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
 * @version 2.2.2
 **/

//Switch to the appropriate trace level
#define TRACE_LEVEL NIC_TRACE_LEVEL

//Dependencies
#include <limits.h>
#include "sam.h"
#include "core/net.h"
#include "drivers/mac/samv71_eth_driver.h"
#include "debug.h"

//Underlying network interface
static NetInterface *nicDriverInterface;

//IAR EWARM compiler?
#if defined(__ICCARM__)

//TX buffer
#pragma data_alignment = 8
#pragma location = SAMV71_ETH_RAM_SECTION
static uint8_t txBuffer[SAMV71_ETH_TX_BUFFER_COUNT][SAMV71_ETH_TX_BUFFER_SIZE];
//RX buffer
#pragma data_alignment = 8
#pragma location = SAMV71_ETH_RAM_SECTION
static uint8_t rxBuffer[SAMV71_ETH_RX_BUFFER_COUNT][SAMV71_ETH_RX_BUFFER_SIZE];
//TX buffer descriptors
#pragma data_alignment = 4
#pragma location = SAMV71_ETH_RAM_SECTION
static Samv71TxBufferDesc txBufferDesc[SAMV71_ETH_TX_BUFFER_COUNT];
//RX buffer descriptors
#pragma data_alignment = 4
#pragma location = SAMV71_ETH_RAM_SECTION
static Samv71RxBufferDesc rxBufferDesc[SAMV71_ETH_RX_BUFFER_COUNT];

//Dummy TX buffer
#pragma data_alignment = 8
#pragma location = SAMV71_ETH_RAM_SECTION
static uint8_t dummyTxBuffer[SAMV71_ETH_DUMMY_BUFFER_COUNT][SAMV71_ETH_DUMMY_BUFFER_SIZE];
//Dummy RX buffer
#pragma data_alignment = 8
#pragma location = SAMV71_ETH_RAM_SECTION
static uint8_t dummyRxBuffer[SAMV71_ETH_DUMMY_BUFFER_COUNT][SAMV71_ETH_DUMMY_BUFFER_SIZE];
//Dummy TX buffer descriptors
#pragma data_alignment = 4
#pragma location = SAMV71_ETH_RAM_SECTION
static Samv71TxBufferDesc dummyTxBufferDesc[SAMV71_ETH_DUMMY_BUFFER_COUNT];
//Dummy RX buffer descriptors
#pragma data_alignment = 4
#pragma location = SAMV71_ETH_RAM_SECTION
static Samv71RxBufferDesc dummyRxBufferDesc[SAMV71_ETH_DUMMY_BUFFER_COUNT];

//Keil MDK-ARM or GCC compiler?
#else

//TX buffer
static uint8_t txBuffer[SAMV71_ETH_TX_BUFFER_COUNT][SAMV71_ETH_TX_BUFFER_SIZE]
   __attribute__((aligned(8), __section__(SAMV71_ETH_RAM_SECTION)));
//RX buffer
static uint8_t rxBuffer[SAMV71_ETH_RX_BUFFER_COUNT][SAMV71_ETH_RX_BUFFER_SIZE]
   __attribute__((aligned(8), __section__(SAMV71_ETH_RAM_SECTION)));
//TX buffer descriptors
static Samv71TxBufferDesc txBufferDesc[SAMV71_ETH_TX_BUFFER_COUNT]
   __attribute__((aligned(4), __section__(SAMV71_ETH_RAM_SECTION)));
//RX buffer descriptors
static Samv71RxBufferDesc rxBufferDesc[SAMV71_ETH_RX_BUFFER_COUNT]
   __attribute__((aligned(4), __section__(SAMV71_ETH_RAM_SECTION)));

//Dummy TX buffer
static uint8_t dummyTxBuffer[SAMV71_ETH_DUMMY_BUFFER_COUNT][SAMV71_ETH_DUMMY_BUFFER_SIZE]
   __attribute__((aligned(8), __section__(SAMV71_ETH_RAM_SECTION)));
//Dummy RX buffer
static uint8_t dummyRxBuffer[SAMV71_ETH_DUMMY_BUFFER_COUNT][SAMV71_ETH_DUMMY_BUFFER_SIZE]
   __attribute__((aligned(8), __section__(SAMV71_ETH_RAM_SECTION)));
//Dummy TX buffer descriptors
static Samv71TxBufferDesc dummyTxBufferDesc[SAMV71_ETH_DUMMY_BUFFER_COUNT]
   __attribute__((aligned(4), __section__(SAMV71_ETH_RAM_SECTION)));
//Dummy RX buffer descriptors
static Samv71RxBufferDesc dummyRxBufferDesc[SAMV71_ETH_DUMMY_BUFFER_COUNT]
   __attribute__((aligned(4), __section__(SAMV71_ETH_RAM_SECTION)));

#endif

//TX buffer index
static uint_t txBufferIndex;
//RX buffer index
static uint_t rxBufferIndex;


/**
 * @brief SAMV71 Ethernet MAC driver
 **/

const NicDriver samv71EthDriver =
{
   NIC_TYPE_ETHERNET,
   ETH_MTU,
   samv71EthInit,
   samv71EthTick,
   samv71EthEnableIrq,
   samv71EthDisableIrq,
   samv71EthEventHandler,
   samv71EthSendPacket,
   samv71EthUpdateMacAddrFilter,
   samv71EthUpdateMacConfig,
   samv71EthWritePhyReg,
   samv71EthReadPhyReg,
   TRUE,
   TRUE,
   TRUE,
   FALSE
};


/**
 * @brief SAMV71 Ethernet MAC initialization
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t samv71EthInit(NetInterface *interface)
{
   error_t error;
   volatile uint32_t status;

   //Debug message
   TRACE_INFO("Initializing SAMV71 Ethernet MAC...\r\n");

   //Save underlying network interface
   nicDriverInterface = interface;

   //Enable GMAC peripheral clock
   PMC_REGS->PMC_PCER1 = (1 << (ID_GMAC - 32));

   //Disable transmit and receive circuits
   GMAC_REGS->GMAC_NCR = 0;

   //GPIO configuration
   samv71EthInitGpio(interface);

   //Configure MDC clock speed
   GMAC_REGS->GMAC_NCFGR = GMAC_NCFGR_CLK_MCK_96;
   //Enable management port (MDC and MDIO)
   GMAC_REGS->GMAC_NCR |= GMAC_NCR_MPE_Msk;

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

   //Set the MAC address of the station
   GMAC_REGS->GMAC_SA[0].GMAC_SAB = interface->macAddr.w[0] | (interface->macAddr.w[1] << 16);
   GMAC_REGS->GMAC_SA[0].GMAC_SAT = interface->macAddr.w[2];

   //The MAC supports 3 additional addresses for unicast perfect filtering
   GMAC_REGS->GMAC_SA[1].GMAC_SAB = 0;
   GMAC_REGS->GMAC_SA[2].GMAC_SAB = 0;
   GMAC_REGS->GMAC_SA[3].GMAC_SAB = 0;

   //Initialize hash table
   GMAC_REGS->GMAC_HRB = 0;
   GMAC_REGS->GMAC_HRT = 0;

   //Configure the receive filter
   GMAC_REGS->GMAC_NCFGR |= GMAC_NCFGR_MAXFS_Msk | GMAC_NCFGR_MTIHEN_Msk;

   //DMA configuration
   GMAC_REGS->GMAC_DCFGR = GMAC_DCFGR_DRBS(SAMV71_ETH_RX_BUFFER_SIZE / 64) |
      GMAC_DCFGR_TXPBMS_Msk | GMAC_DCFGR_RXBMS_FULL | GMAC_DCFGR_FBLDO_INCR4;

   GMAC_REGS->GMAC_RBSRPQ[0] = GMAC_RBSRPQ_RBS(SAMV71_ETH_DUMMY_BUFFER_SIZE / 64);
   GMAC_REGS->GMAC_RBSRPQ[1] = GMAC_RBSRPQ_RBS(SAMV71_ETH_DUMMY_BUFFER_SIZE / 64);
   GMAC_REGS->GMAC_RBSRPQ[2] = GMAC_RBSRPQ_RBS(SAMV71_ETH_DUMMY_BUFFER_SIZE / 64);
   GMAC_REGS->GMAC_RBSRPQ[3] = GMAC_RBSRPQ_RBS(SAMV71_ETH_DUMMY_BUFFER_SIZE / 64);
   GMAC_REGS->GMAC_RBSRPQ[4] = GMAC_RBSRPQ_RBS(SAMV71_ETH_DUMMY_BUFFER_SIZE / 64);

   //Initialize buffer descriptors
   samv71EthInitBufferDesc(interface);

   //Clear transmit status register
   GMAC_REGS->GMAC_TSR = GMAC_TSR_HRESP_Msk | GMAC_TSR_TXCOMP_Msk |
      GMAC_TSR_TFC_Msk | GMAC_TSR_TXGO_Msk | GMAC_TSR_RLE_Msk |
      GMAC_TSR_COL_Msk | GMAC_TSR_UBR_Msk;

   //Clear receive status register
   GMAC_REGS->GMAC_RSR = GMAC_RSR_HNO_Msk | GMAC_RSR_RXOVR_Msk |
      GMAC_RSR_REC_Msk | GMAC_RSR_BNA_Msk;

   //First disable all GMAC interrupts
   GMAC_REGS->GMAC_IDR = 0xFFFFFFFF;
   GMAC_REGS->GMAC_IDRPQ[0] = 0xFFFFFFFF;
   GMAC_REGS->GMAC_IDRPQ[1] = 0xFFFFFFFF;
   GMAC_REGS->GMAC_IDRPQ[2] = 0xFFFFFFFF;
   GMAC_REGS->GMAC_IDRPQ[3] = 0xFFFFFFFF;
   GMAC_REGS->GMAC_IDRPQ[4] = 0xFFFFFFFF;

   //Only the desired ones are enabled
   GMAC_REGS->GMAC_IER = GMAC_IER_HRESP_Msk | GMAC_IER_ROVR_Msk |
      GMAC_IER_TCOMP_Msk | GMAC_IER_TFC_Msk | GMAC_IER_RLEX_Msk |
      GMAC_IER_TUR_Msk | GMAC_IER_RXUBR_Msk | GMAC_IER_RCOMP_Msk;

   //Read GMAC ISR register to clear any pending interrupt
   status = GMAC_REGS->GMAC_ISR;

   //Set priority grouping (3 bits for pre-emption priority, no bits for subpriority)
   NVIC_SetPriorityGrouping(SAMV71_ETH_IRQ_PRIORITY_GROUPING);

   //Configure GMAC interrupt priority
   NVIC_SetPriority(GMAC_IRQn, NVIC_EncodePriority(SAMV71_ETH_IRQ_PRIORITY_GROUPING,
      SAMV71_ETH_IRQ_GROUP_PRIORITY, SAMV71_ETH_IRQ_SUB_PRIORITY));

   //Enable the GMAC to transmit and receive data
   GMAC_REGS->GMAC_NCR |= GMAC_NCR_TXEN_Msk | GMAC_NCR_RXEN_Msk;

   //Accept any packets from the upper layer
   osSetEvent(&interface->nicTxEvent);

   //Successful initialization
   return NO_ERROR;
}


/**
 * @brief GPIO configuration
 * @param[in] interface Underlying network interface
 **/

__weak_func void samv71EthInitGpio(NetInterface *interface)
{
//SAMV71-Xplained-Ultra evaluation board?
#if defined(USE_SAMV71_XPLAINED_ULTRA)
   //Enable PIO peripheral clocks
   PMC_REGS->PMC_PCER0 = (1 << ID_PIOC) | (1 << ID_PIOD);

   //Disable pull-up resistors on RMII pins
   PIOD_REGS->PIO_PUDR = GMAC_RMII_MASK;
   //Disable interrupts-on-change
   PIOD_REGS->PIO_IDR = GMAC_RMII_MASK;
   //Assign RMII pins to peripheral A function
   PIOD_REGS->PIO_ABCDSR[0] &= ~GMAC_RMII_MASK;
   PIOD_REGS->PIO_ABCDSR[1] &= ~GMAC_RMII_MASK;
   //Disable the PIO from controlling the corresponding pins
   PIOD_REGS->PIO_PDR = GMAC_RMII_MASK;

   //Select RMII operation mode
   GMAC_REGS->GMAC_UR &= ~GMAC_UR_RMII_Msk;

   //Configure PHY_RESET as an output
   PIOC_REGS->PIO_PER = PIO_PC10;
   PIOC_REGS->PIO_OER = PIO_PC10;

   //Reset PHY transceiver
   PIOC_REGS->PIO_CODR = PIO_PC10;
   sleep(10);
   PIOC_REGS->PIO_SODR = PIO_PC10;
   sleep(10);
#endif
}


/**
 * @brief Initialize buffer descriptors
 * @param[in] interface Underlying network interface
 **/

void samv71EthInitBufferDesc(NetInterface *interface)
{
   uint_t i;
   uint32_t address;

   //Initialize TX buffer descriptors
   for(i = 0; i < SAMV71_ETH_TX_BUFFER_COUNT; i++)
   {
      //Calculate the address of the current TX buffer
      address = (uint32_t) txBuffer[i];
      //Write the address to the descriptor entry
      txBufferDesc[i].address = address;
      //Initialize status field
      txBufferDesc[i].status = GMAC_TX_USED;
   }

   //Mark the last descriptor entry with the wrap flag
   txBufferDesc[i - 1].status |= GMAC_TX_WRAP;
   //Initialize TX buffer index
   txBufferIndex = 0;

   //Initialize RX buffer descriptors
   for(i = 0; i < SAMV71_ETH_RX_BUFFER_COUNT; i++)
   {
      //Calculate the address of the current RX buffer
      address = (uint32_t) rxBuffer[i];
      //Write the address to the descriptor entry
      rxBufferDesc[i].address = address & GMAC_RX_ADDRESS;
      //Clear status field
      rxBufferDesc[i].status = 0;
   }

   //Mark the last descriptor entry with the wrap flag
   rxBufferDesc[i - 1].address |= GMAC_RX_WRAP;
   //Initialize RX buffer index
   rxBufferIndex = 0;

   //Initialize dummy TX buffer descriptors
   for(i = 0; i < SAMV71_ETH_DUMMY_BUFFER_COUNT; i++)
   {
      //Calculate the address of the current TX buffer
      address = (uint32_t) dummyTxBuffer[i];
      //Write the address to the descriptor entry
      dummyTxBufferDesc[i].address = address;
      //Initialize status field
      dummyTxBufferDesc[i].status = GMAC_TX_USED;
   }

   //Mark the last descriptor entry with the wrap flag
   dummyTxBufferDesc[i - 1].status |= GMAC_TX_WRAP;

   //Initialize dummy RX buffer descriptors
   for(i = 0; i < SAMV71_ETH_DUMMY_BUFFER_COUNT; i++)
   {
      //Calculate the address of the current RX buffer
      address = (uint32_t) dummyRxBuffer[i];
      //Write the address to the descriptor entry
      dummyRxBufferDesc[i].address = (address & GMAC_RX_ADDRESS) | GMAC_RX_OWNERSHIP;
      //Clear status field
      dummyRxBufferDesc[i].status = 0;
   }

   //Mark the last descriptor entry with the wrap flag
   dummyRxBufferDesc[i - 1].address |= GMAC_RX_WRAP;

   //Start location of the TX descriptor list
   GMAC_REGS->GMAC_TBQB = (uint32_t) txBufferDesc;
   GMAC_REGS->GMAC_TBQBAPQ[0] = (uint32_t) dummyTxBufferDesc;
   GMAC_REGS->GMAC_TBQBAPQ[1] = (uint32_t) dummyTxBufferDesc;
   GMAC_REGS->GMAC_TBQBAPQ[2] = (uint32_t) dummyTxBufferDesc;
   GMAC_REGS->GMAC_TBQBAPQ[3] = (uint32_t) dummyTxBufferDesc;
   GMAC_REGS->GMAC_TBQBAPQ[4] = (uint32_t) dummyTxBufferDesc;

   //Start location of the RX descriptor list
   GMAC_REGS->GMAC_RBQB = (uint32_t) rxBufferDesc;
   GMAC_REGS->GMAC_RBQBAPQ[0] = (uint32_t) dummyRxBufferDesc;
   GMAC_REGS->GMAC_RBQBAPQ[1] = (uint32_t) dummyRxBufferDesc;
   GMAC_REGS->GMAC_RBQBAPQ[2] = (uint32_t) dummyRxBufferDesc;
   GMAC_REGS->GMAC_RBQBAPQ[3] = (uint32_t) dummyRxBufferDesc;
   GMAC_REGS->GMAC_RBQBAPQ[4] = (uint32_t) dummyRxBufferDesc;
}


/**
 * @brief SAMV71 Ethernet MAC timer handler
 *
 * This routine is periodically called by the TCP/IP stack to handle periodic
 * operations such as polling the link state
 *
 * @param[in] interface Underlying network interface
 **/

void samv71EthTick(NetInterface *interface)
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

void samv71EthEnableIrq(NetInterface *interface)
{
   //Enable Ethernet MAC interrupts
   NVIC_EnableIRQ(GMAC_IRQn);

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

void samv71EthDisableIrq(NetInterface *interface)
{
   //Disable Ethernet MAC interrupts
   NVIC_DisableIRQ(GMAC_IRQn);

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
 * @brief SAMV71 Ethernet MAC interrupt service routine
 **/

void GMAC_Handler(void)
{
   bool_t flag;
   volatile uint32_t isr;
   volatile uint32_t tsr;
   volatile uint32_t rsr;

   //Interrupt service routine prologue
   osEnterIsr();

   //This flag will be set if a higher priority task must be woken
   flag = FALSE;

   //Each time the software reads GMAC_ISR, it has to check the contents
   //of GMAC_TSR, GMAC_RSR and GMAC_NSR
   isr = GMAC_REGS->GMAC_ISRPQ[0];
   isr = GMAC_REGS->GMAC_ISRPQ[1];
   isr = GMAC_REGS->GMAC_ISRPQ[2];
   isr = GMAC_REGS->GMAC_ISRPQ[3];
   isr = GMAC_REGS->GMAC_ISRPQ[4];
   isr = GMAC_REGS->GMAC_ISR;
   tsr = GMAC_REGS->GMAC_TSR;
   rsr = GMAC_REGS->GMAC_RSR;

   //Packet transmitted?
   if((tsr & (GMAC_TSR_HRESP_Msk | GMAC_TSR_TXCOMP_Msk |
      GMAC_TSR_TFC_Msk | GMAC_TSR_TXGO_Msk | GMAC_TSR_RLE_Msk |
      GMAC_TSR_COL_Msk | GMAC_TSR_UBR_Msk)) != 0)
   {
      //Only clear TSR flags that are currently set
      GMAC_REGS->GMAC_TSR = tsr;

      //Check whether the TX buffer is available for writing
      if((txBufferDesc[txBufferIndex].status & GMAC_TX_USED) != 0)
      {
         //Notify the TCP/IP stack that the transmitter is ready to send
         flag |= osSetEventFromIsr(&nicDriverInterface->nicTxEvent);
      }
   }

   //Packet received?
   if((rsr & (GMAC_RSR_HNO_Msk | GMAC_RSR_RXOVR_Msk | GMAC_RSR_REC_Msk |
      GMAC_RSR_BNA_Msk)) != 0)
   {
      //Set event flag
      nicDriverInterface->nicEvent = TRUE;
      //Notify the TCP/IP stack of the event
      flag |= osSetEventFromIsr(&netEvent);
   }

   //Interrupt service routine epilogue
   osExitIsr(flag);
}


/**
 * @brief SAMV71 Ethernet MAC event handler
 * @param[in] interface Underlying network interface
 **/

void samv71EthEventHandler(NetInterface *interface)
{
   error_t error;
   uint32_t rsr;

   //Read receive status
   rsr = GMAC_REGS->GMAC_RSR;

   //Packet received?
   if((rsr & (GMAC_RSR_HNO_Msk | GMAC_RSR_RXOVR_Msk | GMAC_RSR_REC_Msk |
      GMAC_RSR_BNA_Msk)) != 0)
   {
      //Only clear RSR flags that are currently set
      GMAC_REGS->GMAC_RSR = rsr;

      //Process all pending packets
      do
      {
         //Read incoming packet
         error = samv71EthReceivePacket(interface);

         //No more data in the receive buffer?
      } while(error != ERROR_BUFFER_EMPTY);
   }
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

error_t samv71EthSendPacket(NetInterface *interface,
   const NetBuffer *buffer, size_t offset, NetTxAncillary *ancillary)
{
   static uint8_t temp[SAMV71_ETH_TX_BUFFER_SIZE];
   size_t length;

   //Retrieve the length of the packet
   length = netBufferGetLength(buffer) - offset;

   //Check the frame length
   if(length > SAMV71_ETH_TX_BUFFER_SIZE)
   {
      //The transmitter can accept another packet
      osSetEvent(&interface->nicTxEvent);
      //Report an error
      return ERROR_INVALID_LENGTH;
   }

   //Make sure the current buffer is available for writing
   if((txBufferDesc[txBufferIndex].status & GMAC_TX_USED) == 0)
   {
      return ERROR_FAILURE;
   }

   //Copy user data to the transmit buffer
   netBufferRead(temp, buffer, offset, length);
   osMemcpy(txBuffer[txBufferIndex], temp, (length + 3) & ~3UL);

   //Set the necessary flags in the descriptor entry
   if(txBufferIndex < (SAMV71_ETH_TX_BUFFER_COUNT - 1))
   {
      //Write the status word
      txBufferDesc[txBufferIndex].status = GMAC_TX_LAST |
         (length & GMAC_TX_LENGTH);

      //Point to the next buffer
      txBufferIndex++;
   }
   else
   {
      //Write the status word
      txBufferDesc[txBufferIndex].status = GMAC_TX_WRAP | GMAC_TX_LAST |
         (length & GMAC_TX_LENGTH);

      //Wrap around
      txBufferIndex = 0;
   }

   //Data synchronization barrier
   __DSB();

   //Set the TSTART bit to initiate transmission
   GMAC_REGS->GMAC_NCR |= GMAC_NCR_TSTART_Msk;

   //Check whether the next buffer is available for writing
   if((txBufferDesc[txBufferIndex].status & GMAC_TX_USED) != 0)
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

error_t samv71EthReceivePacket(NetInterface *interface)
{
   static uint8_t temp[ETH_MAX_FRAME_SIZE];
   error_t error;
   uint_t i;
   uint_t j;
   uint_t sofIndex;
   uint_t eofIndex;
   size_t n;
   size_t size;
   size_t length;

   //Initialize SOF and EOF indices
   sofIndex = UINT_MAX;
   eofIndex = UINT_MAX;

   //Search for SOF and EOF flags
   for(i = 0; i < SAMV71_ETH_RX_BUFFER_COUNT; i++)
   {
      //Point to the current entry
      j = rxBufferIndex + i;

      //Wrap around to the beginning of the buffer if necessary
      if(j >= SAMV71_ETH_RX_BUFFER_COUNT)
      {
         j -= SAMV71_ETH_RX_BUFFER_COUNT;
      }

      //No more entries to process?
      if((rxBufferDesc[j].address & GMAC_RX_OWNERSHIP) == 0)
      {
         //Stop processing
         break;
      }

      //A valid SOF has been found?
      if((rxBufferDesc[j].status & GMAC_RX_SOF) != 0)
      {
         //Save the position of the SOF
         sofIndex = i;
      }

      //A valid EOF has been found?
      if((rxBufferDesc[j].status & GMAC_RX_EOF) != 0 && sofIndex != UINT_MAX)
      {
         //Save the position of the EOF
         eofIndex = i;
         //Retrieve the length of the frame
         size = rxBufferDesc[j].status & GMAC_RX_LENGTH;
         //Limit the number of data to read
         size = MIN(size, ETH_MAX_FRAME_SIZE);
         //Stop processing since we have reached the end of the frame
         break;
      }
   }

   //Determine the number of entries to process
   if(eofIndex != UINT_MAX)
   {
      j = eofIndex + 1;
   }
   else if(sofIndex != UINT_MAX)
   {
      j = sofIndex;
   }
   else
   {
      j = i;
   }

   //Total number of bytes that have been copied from the receive buffer
   length = 0;

   //Process incoming frame
   for(i = 0; i < j; i++)
   {
      //Any data to copy from current buffer?
      if(eofIndex != UINT_MAX && i >= sofIndex && i <= eofIndex)
      {
         //Calculate the number of bytes to read at a time
         n = MIN(size, SAMV71_ETH_RX_BUFFER_SIZE);
         //Copy data from receive buffer
         osMemcpy(temp + length, rxBuffer[rxBufferIndex], (n + 3) & ~3UL);
         //Update byte counters
         length += n;
         size -= n;
      }

      //Mark the current buffer as free
      rxBufferDesc[rxBufferIndex].address &= ~GMAC_RX_OWNERSHIP;

      //Point to the following entry
      rxBufferIndex++;

      //Wrap around to the beginning of the buffer if necessary
      if(rxBufferIndex >= SAMV71_ETH_RX_BUFFER_COUNT)
      {
         rxBufferIndex = 0;
      }
   }

   //Any packet to process?
   if(length > 0)
   {
      NetRxAncillary ancillary;

      //Additional options can be passed to the stack along with the packet
      ancillary = NET_DEFAULT_RX_ANCILLARY;

      //Pass the packet to the upper layer
      nicProcessPacket(interface, temp, length, &ancillary);
      //Valid packet received
      error = NO_ERROR;
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

error_t samv71EthUpdateMacAddrFilter(NetInterface *interface)
{
   uint_t i;
   uint_t j;
   uint_t k;
   uint8_t *p;
   uint32_t hashTable[2];
   MacAddr unicastMacAddr[3];
   MacFilterEntry *entry;

   //Debug message
   TRACE_DEBUG("Updating MAC filter...\r\n");

   //Set the MAC address of the station
   GMAC_REGS->GMAC_SA[0].GMAC_SAB = interface->macAddr.w[0] | (interface->macAddr.w[1] << 16);
   GMAC_REGS->GMAC_SA[0].GMAC_SAT = interface->macAddr.w[2];

   //The MAC supports 3 additional addresses for unicast perfect filtering
   unicastMacAddr[0] = MAC_UNSPECIFIED_ADDR;
   unicastMacAddr[1] = MAC_UNSPECIFIED_ADDR;
   unicastMacAddr[2] = MAC_UNSPECIFIED_ADDR;

   //The hash table is used for multicast address filtering
   hashTable[0] = 0;
   hashTable[1] = 0;

   //The MAC address filter contains the list of MAC addresses to accept
   //when receiving an Ethernet frame
   for(i = 0, j = 0; i < MAC_ADDR_FILTER_SIZE; i++)
   {
      //Point to the current entry
      entry = &interface->macAddrFilter[i];

      //Valid entry?
      if(entry->refCount > 0)
      {
         //Multicast address?
         if(macIsMulticastAddr(&entry->addr))
         {
            //Point to the MAC address
            p = entry->addr.b;

            //Apply the hash function
            k = (p[0] >> 6) ^ p[0];
            k ^= (p[1] >> 4) ^ (p[1] << 2);
            k ^= (p[2] >> 2) ^ (p[2] << 4);
            k ^= (p[3] >> 6) ^ p[3];
            k ^= (p[4] >> 4) ^ (p[4] << 2);
            k ^= (p[5] >> 2) ^ (p[5] << 4);

            //The hash value is reduced to a 6-bit index
            k &= 0x3F;

            //Update hash table contents
            hashTable[k / 32] |= (1 << (k % 32));
         }
         else
         {
            //Up to 3 additional MAC addresses can be specified
            if(j < 3)
            {
               //Save the unicast address
               unicastMacAddr[j] = entry->addr;
            }
            else
            {
               //Point to the MAC address
               p = entry->addr.b;

               //Apply the hash function
               k = (p[0] >> 6) ^ p[0];
               k ^= (p[1] >> 4) ^ (p[1] << 2);
               k ^= (p[2] >> 2) ^ (p[2] << 4);
               k ^= (p[3] >> 6) ^ p[3];
               k ^= (p[4] >> 4) ^ (p[4] << 2);
               k ^= (p[5] >> 2) ^ (p[5] << 4);

               //The hash value is reduced to a 6-bit index
               k &= 0x3F;

               //Update hash table contents
               hashTable[k / 32] |= (1 << (k % 32));
            }

            //Increment the number of unicast addresses
            j++;
         }
      }
   }

   //Configure the first unicast address filter
   if(j >= 1)
   {
      //The address is activated when SAT register is written
      GMAC_REGS->GMAC_SA[1].GMAC_SAB = unicastMacAddr[0].w[0] | (unicastMacAddr[0].w[1] << 16);
      GMAC_REGS->GMAC_SA[1].GMAC_SAT = unicastMacAddr[0].w[2];
   }
   else
   {
      //The address is deactivated when SAB register is written
      GMAC_REGS->GMAC_SA[1].GMAC_SAB = 0;
   }

   //Configure the second unicast address filter
   if(j >= 2)
   {
      //The address is activated when SAT register is written
      GMAC_REGS->GMAC_SA[2].GMAC_SAB = unicastMacAddr[1].w[0] | (unicastMacAddr[1].w[1] << 16);
      GMAC_REGS->GMAC_SA[2].GMAC_SAT = unicastMacAddr[1].w[2];
   }
   else
   {
      //The address is deactivated when SAB register is written
      GMAC_REGS->GMAC_SA[2].GMAC_SAB = 0;
   }

   //Configure the third unicast address filter
   if(j >= 3)
   {
      //The address is activated when SAT register is written
      GMAC_REGS->GMAC_SA[3].GMAC_SAB = unicastMacAddr[2].w[0] | (unicastMacAddr[2].w[1] << 16);
      GMAC_REGS->GMAC_SA[3].GMAC_SAT = unicastMacAddr[2].w[2];
   }
   else
   {
      //The address is deactivated when SAB register is written
      GMAC_REGS->GMAC_SA[3].GMAC_SAB = 0;
   }

   //The perfect MAC filter supports only 3 unicast addresses
   if(j >= 4)
   {
      GMAC_REGS->GMAC_NCFGR |= GMAC_NCFGR_UNIHEN_Msk;
   }
   else
   {
      GMAC_REGS->GMAC_NCFGR &= ~GMAC_NCFGR_UNIHEN_Msk;
   }

   //Configure the multicast hash table
   GMAC_REGS->GMAC_HRB = hashTable[0];
   GMAC_REGS->GMAC_HRT = hashTable[1];

   //Debug message
   TRACE_DEBUG("  HRB = %08" PRIX32 "\r\n", GMAC_REGS->GMAC_HRB);
   TRACE_DEBUG("  HRT = %08" PRIX32 "\r\n", GMAC_REGS->GMAC_HRT);

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Adjust MAC configuration parameters for proper operation
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t samv71EthUpdateMacConfig(NetInterface *interface)
{
   uint32_t config;

   //Read network configuration register
   config = GMAC_REGS->GMAC_NCFGR;

   //10BASE-T or 100BASE-TX operation mode?
   if(interface->linkSpeed == NIC_LINK_SPEED_100MBPS)
   {
      config |= GMAC_NCFGR_SPD_Msk;
   }
   else
   {
      config &= ~GMAC_NCFGR_SPD_Msk;
   }

   //Half-duplex or full-duplex mode?
   if(interface->duplexMode == NIC_FULL_DUPLEX_MODE)
   {
      config |= GMAC_NCFGR_FD_Msk;
   }
   else
   {
      config &= ~GMAC_NCFGR_FD_Msk;
   }

   //Write configuration value back to NCFGR register
   GMAC_REGS->GMAC_NCFGR = config;

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

void samv71EthWritePhyReg(uint8_t opcode, uint8_t phyAddr,
   uint8_t regAddr, uint16_t data)
{
   uint32_t temp;

   //Valid opcode?
   if(opcode == SMI_OPCODE_WRITE)
   {
      //Set up a write operation
      temp = GMAC_MAN_CLTTO_Msk | GMAC_MAN_OP(1) | GMAC_MAN_WTN(2);
      //PHY address
      temp |= GMAC_MAN_PHYA(phyAddr);
      //Register address
      temp |= GMAC_MAN_REGA(regAddr);
      //Register value
      temp |= GMAC_MAN_DATA(data);

      //Start a write operation
      GMAC_REGS->GMAC_MAN = temp;
      //Wait for the write to complete
      while((GMAC_REGS->GMAC_NSR & GMAC_NSR_IDLE_Msk) == 0)
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

uint16_t samv71EthReadPhyReg(uint8_t opcode, uint8_t phyAddr,
   uint8_t regAddr)
{
   uint16_t data;
   uint32_t temp;

   //Valid opcode?
   if(opcode == SMI_OPCODE_READ)
   {
      //Set up a read operation
      temp = GMAC_MAN_CLTTO_Msk | GMAC_MAN_OP(2) | GMAC_MAN_WTN(2);
      //PHY address
      temp |= GMAC_MAN_PHYA(phyAddr);
      //Register address
      temp |= GMAC_MAN_REGA(regAddr);

      //Start a read operation
      GMAC_REGS->GMAC_MAN = temp;
      //Wait for the read to complete
      while((GMAC_REGS->GMAC_NSR & GMAC_NSR_IDLE_Msk) == 0)
      {
      }

      //Get register value
      data = GMAC_REGS->GMAC_MAN & GMAC_MAN_DATA_Msk;
   }
   else
   {
      //The MAC peripheral only supports standard Clause 22 opcodes
      data = 0;
   }

   //Return the value of the PHY register
   return data;
}
