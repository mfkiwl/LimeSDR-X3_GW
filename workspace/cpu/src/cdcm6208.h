/*
 * cdcm6208.h
 *
 * Functions for interacting with cdcm6208 chip
 *
 *
 *  Created on: Mar 17, 2021
 *      Author: tservenikas
 */

#include "xspi.h"		/* SPI device driver */
#include <stdint.h>

void Write_to_CDCM(XSpi *InstancePtr, u32 slave_mask, uint16_t Address, uint16_t Value);
uint16_t Read_from_CDCM(XSpi *InstancePtr, u32 slave_mask, uint16_t Address);
void config_CDCM(XSpi *CDCMInstancePtr, uint32_t CDCMslave_mask,XSpi *CFGInstancePtr, uint32_t CFGslave_mask, uint16_t cdcmcfg_base);
void Check_CDCM_Update(XSpi *CDCMInstancePtr, uint32_t CDCMslave_mask,XSpi *CFGInstancePtr, uint32_t CFGslave_mask, uint16_t cdcmcfg_base, uint8_t lock_timeout);
void ReadALLCDCM_Registers(XSpi *CDCMInstancePtr, uint32_t CDCMslave_mask,XSpi *CFGInstancePtr, uint32_t CFGslave_mask, uint16_t cdcmcfg_base);