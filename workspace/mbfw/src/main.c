/*
 * main.c
 */

#include <stdio.h>
#include <stdbool.h>
#include "platform.h"
#include "xil_printf.h"
#include <xgpio.h> 		/* GPIO driver*/
#include <xiic.h>		/* I2C driver*/
#include "xspi.h"		/* SPI device driver */
#include "AXI_to_native_FIFO.h" /* Native FIFO driver*/
#include <xclk_wiz.h>

#include "CDCM6208_ini.h" /* CDCM6208 initialization registers*/
#include "LMS64C_protocol.h"
#include "PCIe_5GRadio_brd.h"
#include "pll_rcfg.h"
#include "vctcxo_tamer.h"
#include "ADS4246_reg.h"

/************************** Constant Definitions *****************************/
/*
 * The following constants map to the XPAR parameters created in the
 * xparameters.h file. They are defined here such that a user can easily
 * change all the needed parameters in one place.
 */
#define SPI0_DEVICE_ID			XPAR_SPI_0_DEVICE_ID
#define SPI1_DEVICE_ID			XPAR_SPI_1_DEVICE_ID
#define SPI2_DEVICE_ID			XPAR_SPI_2_DEVICE_ID

/*
 * The following constant defines the slave select signal that is used to
 * to select the  device on the SPI bus, this signal is typically
 * connected to the chip select of the device.
 */
#define SPI0_FPGA_SS		0x01
#define SPI0_LMS7002M_1_SS	0x02
#define SPI0_LMS7002M_2_SS	0x04
#define SPI0_LMS7002M_3_SS	0x08

#define SPI1_LMS2_BB_ADC1_SS 0x01
#define SPI1_LMS2_BB_ADC2_SS 0x02
#define SPI1_LMS3_BB_ADC1_SS 0x04
#define SPI1_LMS3_BB_ADC2_SS 0x08
#define SPI1_CDCM1_SS 		 0x10
#define SPI1_CDCM2_SS 		 0x20

#define SPI2_TCXO_DAC_SS	0x01
#define SPI2_ADF_SS         0x02
#define SPI1_ADC_SS			0x02



#define SPI2_BB_ADC_SS   	0x01
#define CDCM_SPI2_SELECT 	0x02

#define BRD_SPI_REG_LMS1_LMS2_CTRL  0x13
#define LMS1_SS			0
#define LMS1_RESET		1
#define LMS2_SS			8
#define LMS2_RESET		9

/*
* The following constants are part of clock dynamic reconfiguration
* They are only defined here such that a user can easily change
* needed parameters
*/

#define CLK_LOCK			1

/*FIXED Value */
#define VCO_FREQ			600
#define CLK_WIZ_VCO_FACTOR		(VCO_FREQ * 10000)

 /*Input frequency in MHz */
#define DYNAMIC_INPUT_FREQ		100
#define DYNAMIC_INPUT_FREQ_FACTOR	(DYNAMIC_INPUT_FREQ * 10000)

/*
 * Output frequency in MHz. User need to change this value to
 * generate grater/lesser interrupt as per input frequency
 */
#define DYNAMIC_OUTPUT_FREQ		25
#define DYNAMIC_OUTPUT_FREQFACTOR	(DYNAMIC_OUTPUT_FREQ * 10000)

#define CLK_WIZ_RECONFIG_OUTPUT		DYNAMIC_OUTPUT_FREQ
#define CLK_FRAC_EN			1

uint16_t dac_val = 30714;		//TCXO DAC value
signed short int converted_val = 300;	//Temperature


/************************** Variable Definitions *****************************/

/*
 * The instances to support the device drivers are global such that they
 * are initialized to zero each time the program runs. They could be local
 * but should at least be static so they are zeroed.
 */
static XSpi Spi0, Spi1, Spi2;
static XGpio gpio, pll_rst, pllcfg_cmd, pllcfg_stat, extm_0_axi_sel, smpl_cmp_sel, smpl_cmp_en, smpl_cmp_status;
static XGpio vctcxo_tamer_ctrl;
XClk_Wiz ClkWiz_Dynamic; /* The instance of the ClkWiz_Dynamic */


#define sbi(p,n) ((p) |= (1UL << (n)))
#define cbi(p,n) ((p) &= ~(1 << (n)))

#define FW_VER			1 //Initial version




uint8_t test, block, cmd_errors, glEp0Buffer_Rx[64], glEp0Buffer_Tx[64];
tLMS_Ctrl_Packet *LMS_Ctrl_Packet_Tx = (tLMS_Ctrl_Packet*)glEp0Buffer_Tx;
tLMS_Ctrl_Packet *LMS_Ctrl_Packet_Rx = (tLMS_Ctrl_Packet*)glEp0Buffer_Rx;

/**	This function checks if all blocks could fit in data field.
*	If blocks will not fit, function returns TRUE. */
unsigned char Check_many_blocks (unsigned char block_size)
{
	if (LMS_Ctrl_Packet_Rx->Header.Data_blocks > (sizeof(LMS_Ctrl_Packet_Tx->Data_field)/block_size))
	{
		LMS_Ctrl_Packet_Tx->Header.Status = STATUS_BLOCKS_ERROR_CMD;
		return 1;
	}
	else return 0;
	return 1;
}

void Write_to_CDCM(uint16_t Address, uint16_t Value, char CDCM_NR)
{
	int spi_status;
	uint8_t data[4];
	data[0] = (Address >>8);
	data[1] = (Address & 0xFF);
	data[2] = (Value >> 8);
	data[3] = (Value & 0xFF);

	switch(CDCM_NR) {
	case 2:
		/*
		 * Select the CDCM device,  so that it can be
		 * read and written using the SPI bus.
		 */
		spi_status = XSpi_SetSlaveSelect(&Spi1, SPI1_CDCM2_SS);

		/*
		 * Write to CDCM device
		 */
		spi_status = XSpi_Transfer(&Spi1, data, NULL, 4);
		break;
	default:
		/*
		 * Select the CDCM device,  so that it can be
		 * read and written using the SPI bus.
		 */
		spi_status = XSpi_SetSlaveSelect(&Spi1, SPI1_CDCM1_SS);

		/*
		 * Write to CDCM device
		 */
		spi_status = XSpi_Transfer(&Spi1, data, NULL, 4);
		break;


	}

}

void Init_CDCM(XSpi *InstancePtr, char CDCM_NR)
{
	int spi_status;

	/* Set SPI 0 0 mode*/
	spi_status = XSpi_SetOptions(InstancePtr, XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION);

	switch (CDCM_NR) {
		case 2:
			// CDCM 2 Configuration (External DAC and ADC clocks)
		    Write_to_CDCM( 0,reg_0_data, CDCM_NR);
		    Write_to_CDCM( 1,reg_1_data, CDCM_NR);
		    Write_to_CDCM( 2,reg_2_data, CDCM_NR);
		    Write_to_CDCM( 3,reg_3_data, CDCM_NR);
		    Write_to_CDCM( 4,reg_4_data, CDCM_NR);
		    Write_to_CDCM( 5,reg_5_data, CDCM_NR);
		    Write_to_CDCM( 6,reg_6_data, CDCM_NR);
		    Write_to_CDCM( 7,reg_7_data, CDCM_NR);
		    Write_to_CDCM( 8,reg_8_data, CDCM_NR);
		    Write_to_CDCM( 9,reg_9_data, CDCM_NR);
		    Write_to_CDCM(10,reg_10_data, CDCM_NR);
		    Write_to_CDCM(11,reg_11_data, CDCM_NR);
		    Write_to_CDCM(12,reg_12_data, CDCM_NR);
		    Write_to_CDCM(13,reg_13_data, CDCM_NR);
		    Write_to_CDCM(14,reg_14_data, CDCM_NR);
		    Write_to_CDCM(15,reg_15_data, CDCM_NR);
		    Write_to_CDCM(16,reg_16_data, CDCM_NR);
		    Write_to_CDCM(17,reg_17_data, CDCM_NR);
		    Write_to_CDCM(18,reg_18_data, CDCM_NR);
		    Write_to_CDCM(19,reg_19_data, CDCM_NR);
		    Write_to_CDCM(20,reg_20_data, CDCM_NR);
		    Write_to_CDCM(21,0x0000, CDCM_NR);
		    Write_to_CDCM(40,0x0000, CDCM_NR);
		    break;
		default :
			// TODO: Replace with correct register values
			// CDCM 1 Configuration
		    Write_to_CDCM( 0,0x0231, CDCM_NR);
		    Write_to_CDCM( 1,0x0000, CDCM_NR);
		    Write_to_CDCM( 2,0x0018, CDCM_NR);
		    Write_to_CDCM( 3,0x00F0, CDCM_NR);
		    Write_to_CDCM( 4,0x30AF, CDCM_NR);
		    Write_to_CDCM( 5,0x0001, CDCM_NR);
		    Write_to_CDCM( 6,0x0018, CDCM_NR);
		    Write_to_CDCM( 7,0x0003, CDCM_NR);
		    Write_to_CDCM( 8,0x0018, CDCM_NR);
		    Write_to_CDCM( 9,0x4003, CDCM_NR);
		    Write_to_CDCM(10,0x0000, CDCM_NR);
		    Write_to_CDCM(11,0x0000, CDCM_NR);
		    Write_to_CDCM(12,0x0001, CDCM_NR);
		    Write_to_CDCM(13,0x0000, CDCM_NR);
		    Write_to_CDCM(14,0x0000, CDCM_NR);
		    Write_to_CDCM(15,0x0001, CDCM_NR);
		    Write_to_CDCM(16,0x0000, CDCM_NR);
		    Write_to_CDCM(17,0x0000, CDCM_NR);
		    Write_to_CDCM(18,0x0001, CDCM_NR);
		    Write_to_CDCM(19,0x0000, CDCM_NR);
		    Write_to_CDCM(20,0x0000, CDCM_NR);
		    Write_to_CDCM(21,0x0000, CDCM_NR);
		    Write_to_CDCM(40,0x0000, CDCM_NR);
			break;

	}

}

void Init_SPI(u16 DeviceId, XSpi *InstancePtr, u32 Options)
{
	int spi_status;
	XSpi_Config *ConfigPtr;

	/*
	 * Initialize the SPI driver so that it is  ready to use.
	 */
	ConfigPtr = XSpi_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		//return XST_DEVICE_NOT_FOUND;
	}

	spi_status = XSpi_CfgInitialize(InstancePtr, ConfigPtr,
				  ConfigPtr->BaseAddress);
	if (spi_status != XST_SUCCESS) {
		//return XST_FAILURE;
	}

	/*
	 * Set the SPI device as a master and in manual slave select mode such
	 * that the slave select signal does not toggle for every byte of a
	 * transfer, this must be done before the slave select is set.
	 */
	spi_status = XSpi_SetOptions(InstancePtr, Options);
	if(spi_status != XST_SUCCESS) {
		//return XST_FAILURE;
	}

	// Start the SPI driver so that interrupts and the device are enabled
	spi_status = XSpi_Start(InstancePtr);

	//disable global interrupts since we will use a polled approach
	XSpi_IntrGlobalDisable(InstancePtr);


}

/** Cchecks if peripheral ID is valid.
 Returns 1 if valid, else 0. */
unsigned char Check_Periph_ID (unsigned char max_periph_id, unsigned char Periph_ID)
{
		if (LMS_Ctrl_Packet_Rx->Header.Periph_ID > max_periph_id)
		{
		LMS_Ctrl_Packet_Tx->Header.Status = STATUS_INVALID_PERIPH_ID_CMD;
		return 0;
		}
	else return 1;
}

/**
 * Gets 64 bytes packet from FIFO.
 */
void getFifoData(uint8_t *buf, uint8_t k)
{
	uint8_t cnt = 0;
	uint32_t* dest = (uint32_t*)buf;
	for(cnt=0; cnt<k/sizeof(uint32_t); ++cnt)
	{
		dest[cnt] = AXI_TO_NATIVE_FIFO_mReadReg(XPAR_AXI_TO_NATIVE_FIFO_0_S00_AXI_BASEADDR, AXI_TO_NATIVE_FIFO_S00_AXI_SLV_REG1_OFFSET);
	};
}

/**
 * Configures LM75
 */
void Configure_LM75(void)
{
	int rez;
	int ByteCount;
	u8 WriteBuffer[3];

//	// OS polarity configuration
//	spirez = I2C_start(I2C_OPENCORES_0_BASE, LM75_I2C_ADDR, 0);
//	spirez = I2C_write(I2C_OPENCORES_0_BASE, 0x01, 0);				// Pointer = configuration register
//	//spirez = I2C_start(I2C_OPENCORES_0_BASE, LM75_I2C_ADDR, 1);
//	spirez = I2C_write(I2C_OPENCORES_0_BASE, 0x04, 1);				//Configuration value: OS polarity = 1, Comparator/int = 0, Shutdown = 0

	ByteCount = 2;
	WriteBuffer[0] = 0x01;	// Pointer = configuration register
	WriteBuffer[1] = 0x04;	//Configuration value: OS polarity = 1, Comparator/int = 0, Shutdown = 0
	rez = XIic_Send(XPAR_AXI_IIC_0_BASEADDR, LM75_I2C_ADDR, (u8 *)&WriteBuffer, ByteCount, XIIC_STOP);


//	// THYST configuration
//	spirez = I2C_start(I2C_OPENCORES_0_BASE, LM75_I2C_ADDR, 0);
//	spirez = I2C_write(I2C_OPENCORES_0_BASE, 0x02, 0);				// Pointer = THYST register
//	//spirez = I2C_start(I2C_OPENCORES_0_BASE, LM75_I2C_ADDR, 1);
//	spirez = I2C_write(I2C_OPENCORES_0_BASE, 45, 0);				// Set THYST H
//	spirez = I2C_write(I2C_OPENCORES_0_BASE,  0, 1);				// Set THYST L

	ByteCount = 3;
	WriteBuffer[0] = 0x02;  // Pointer = THYST register
	WriteBuffer[1] = 45;	// Set THYST H
	WriteBuffer[2] = 0;		// Set THYST L
	rez = XIic_Send(XPAR_AXI_IIC_0_BASEADDR, LM75_I2C_ADDR, (u8 *)&WriteBuffer, ByteCount, XIIC_STOP);



//
//	// TOS configuration
//	spirez = I2C_start(I2C_OPENCORES_0_BASE, LM75_I2C_ADDR, 0);
//	spirez = I2C_write(I2C_OPENCORES_0_BASE, 0x03, 0);				// Pointer = TOS register
//	//spirez = I2C_start(I2C_OPENCORES_0_BASE, LM75_I2C_ADDR, 1);
//	spirez = I2C_write(I2C_OPENCORES_0_BASE, 55, 0);				// Set TOS H
//	spirez = I2C_write(I2C_OPENCORES_0_BASE,  0, 1);				// Set TOS L

	ByteCount = 3;
	WriteBuffer[0] = 0x03;	// Pointer = TOS register
	WriteBuffer[1] = 55;	// Set TOS H
	WriteBuffer[2] = 0;		// Set TOS L
	rez = XIic_Send(XPAR_AXI_IIC_0_BASEADDR, LM75_I2C_ADDR, (u8 *)&WriteBuffer, ByteCount, XIIC_STOP);

}



void TestMode_ADC()
{
	uint8_t wr_buf[2];
	uint8_t rd_buf[2];
	int spirez;

	/* Set SPI 1 0 mode */
	spirez = XSpi_SetOptions(&Spi2, XSP_MASTER_OPTION | XSP_CLK_ACTIVE_LOW_OPTION | XSP_MANUAL_SSELECT_OPTION);

	// Disable ADC readout
	wr_buf[0] = 0x00;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, NULL, 2);

	// 0x25
	wr_buf[0] = 0x25;	//Address
	wr_buf[1] = ADS4246_R25_DIG_RAMP;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, NULL, 2);

	// 0x2B
	wr_buf[0] = 0x2B;	//Address
	wr_buf[1] = ADS4246_R2B_DIG_RAMP;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, NULL, 2);

	// 0x3F
	wr_buf[0] = 0x3F;	//Address
	wr_buf[1] = 0x2A;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, NULL, 2);

	// 0x40
	wr_buf[0] = 0x40;	//Address
	wr_buf[1] = 0xAA;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, NULL, 2);


	// 0x42 Enable Digital functions
	wr_buf[0] = 0x42;	//Address
	wr_buf[1] = ( ADS4246_R42_EN_DIGITAL | ADS4246_R42_CLKOUT_RISE_450 | ADS4246_R42_CLKOUT_FALL_450);	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, NULL, 2);

}

void init_ADC(XSpi *InstancePtr, u32 SlaveMask)
{
	uint8_t wr_buf[2];
	uint8_t rd_buf[2];
	int spirez;
	char rst_mask;

	switch (SlaveMask) {
		case SPI1_LMS2_BB_ADC1_SS:
			rst_mask = 0x01;
		case SPI1_LMS2_BB_ADC2_SS:
			rst_mask = 0x02;
			break;
		case SPI1_LMS3_BB_ADC1_SS:
			rst_mask = 0x04;
			break;
		case SPI1_LMS3_BB_ADC2_SS:
			rst_mask = 0x08;
			break;
		default :
			rst_mask = 0x00;
			break;
	}

	/* Set SPI 1 0 mode */
	spirez = XSpi_SetOptions(InstancePtr, XSP_MASTER_OPTION | XSP_CLK_ACTIVE_LOW_OPTION | XSP_MANUAL_SSELECT_OPTION);

	// Set ADC reset to 0
	XGpio_DiscreteWrite(&gpio, 2 , 0x00);
	asm("nop"); asm("nop"); asm("nop"); asm("nop");
	XGpio_DiscreteWrite(&gpio, 2 , rst_mask);
	asm("nop"); asm("nop"); asm("nop"); asm("nop");
	XGpio_DiscreteWrite(&gpio, 2 , 0x00);
	asm("nop"); asm("nop"); asm("nop"); asm("nop");

	// Select BB ADC on SPI2
	spirez = XSpi_SetSlaveSelect(InstancePtr, SlaveMask);


	// Disable ADC readout and reset
	wr_buf[0] = 0x00;	//Address
	wr_buf[1] = 0x02;	//Data
	//wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);


	// 0x01
	wr_buf[0] = 0x01;	//Address
	wr_buf[1] = ADS4246_R01_LVDS_SWING_DEF;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x03
	wr_buf[0] = 0x03;	//Address
	//wr_buf[1] = 0x53;	//Data
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x25
	wr_buf[0] = 0x25;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x29
	wr_buf[0] = 0x29;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x2B
	wr_buf[0] = 0x2B;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x3D
	wr_buf[0] = 0x3D;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x3F
	wr_buf[0] = 0x3F;	//Address
	wr_buf[1] = 0x2A;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x40
	wr_buf[0] = 0x40;	//Address
	wr_buf[1] = 0xAA;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x41
	wr_buf[0] = 0x41;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x42
	wr_buf[0] = 0x42;	//Address
	//wr_buf[1] = 0x08;	//Data
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x45
	wr_buf[0] = 0x45;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x4A
	wr_buf[0] = 0x4A;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0x58
	wr_buf[0] = 0x58;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0xBF
	wr_buf[0] = 0xBF;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0xC1
	wr_buf[0] = 0xC1;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0xCF
	wr_buf[0] = 0xCF;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0xDB
	wr_buf[0] = 0xDB;	//Address
	wr_buf[1] = 0x01;	//Data (0x01 - Low Speed MODE CH B enabled, 0x00 - Low Speed MODE CH B disabled)
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0xEF
	wr_buf[0] = 0xEF;	//Address
	wr_buf[1] = 0x10;	//Data (0x10 - Low Speed MODE enabled, 0x00 - Low Speed MODE disabled)
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0xF1
	wr_buf[0] = 0xF1;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// 0xF2
	wr_buf[0] = 0xF2;	//Address
	wr_buf[1] = 0x08;	//Data (0x08 - Low Speed MODE CH A enabled, 0x00 - Low Speed MODE CH A disabled)
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(InstancePtr, wr_buf, NULL, 2);

	// ---------------Testing
	// Enable ADC readout

	/*
	wr_buf[0] = 0x00;	//Address
	wr_buf[1] = 0x01;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, NULL, 2);

	wr_buf[0] = 0x3F;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0x40;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0x41;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0x42;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0x45;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0x4A;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0x58;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0xBF;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0xC1;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0xCF;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0xDB;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0xEF;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0xF1;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	wr_buf[0] = 0xF2;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 1, wr_buf, 1, rd_buf, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);

	// Disable ADC readout
	wr_buf[0] = 0x00;	//Address
	wr_buf[1] = 0x00;	//Data
	//spirez = alt_avalon_spi_command(SPI_2_BASE, SPI_2_NR_EXTADC, 2, wr_buf, 0, NULL, 0);
	spirez = XSpi_Transfer(&Spi2, wr_buf, rd_buf, 2);
	*/





}






/**
 *	@brief Function to modify BRD (FPGA) spi register bits
 *	@param SPI_reg_addr register address
 *	@param MSB_bit MSB bit of range that will be modified
 *	@param LSB_bit LSB bit of range that will be modified
 */
void Modify_BRDSPI16_Reg_bits (unsigned short int SPI_reg_addr, unsigned char MSB_bit, unsigned char LSB_bit, unsigned short int new_bits_data)
{
	unsigned short int mask, SPI_reg_data;
	unsigned char bits_number;
	//uint8_t MSB_byte, LSB_byte;
	unsigned char WrBuff[4];
	unsigned char RdBuff[4];
	int spirez;

	//**Reconfigure_SPI_for_LMS();

	bits_number = MSB_bit - LSB_bit + 1;

	mask = 0xFFFF;

	//removing unnecessary bits from mask
	mask = mask << (16 - bits_number);
	mask = mask >> (16 - bits_number);

	new_bits_data &= mask; //mask new data

	new_bits_data = new_bits_data << LSB_bit; //shift new data

	mask = mask << LSB_bit; //shift mask
	mask =~ mask;//invert mask

	// Read original data
	WrBuff[0] = (SPI_reg_addr >> 8 ) & 0xFF; //MSB_byte
	WrBuff[1] = SPI_reg_addr & 0xFF; //LSB_byte
	cbi(WrBuff[0], 7);  //clear write bit
	//spirez = alt_avalon_spi_command(FPGA_SPI0_BASE, SPI_NR_FPGA, 2, WrBuff, 2, RdBuff, 0);
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, WrBuff, RdBuff, 4);

	//SPI_reg_data = (RdBuff[0] << 8) + RdBuff[1]; //read current SPI reg data
	// we are reading 4 bytes
	SPI_reg_data = (RdBuff[2] << 8) + RdBuff[3]; //read current SPI reg data

	//modify reg data
	SPI_reg_data &= mask;//clear bits
	SPI_reg_data |= new_bits_data; //set bits with new data

	//write reg addr
	WrBuff[0] = (SPI_reg_addr >> 8 ) & 0xFF; //MSB_byte
	WrBuff[1] = SPI_reg_addr & 0xFF; //LSB_byte
	//modified data to be written to SPI reg
	WrBuff[2] = (SPI_reg_data >> 8 ) & 0xFF;
	WrBuff[3] = SPI_reg_data & 0xFF;
	sbi(WrBuff[0], 7); //set write bit
	//spirez = alt_avalon_spi_command(FPGA_SPI0_BASE, SPI_NR_FPGA, 4, WrBuff, 0, NULL, 0);
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, WrBuff, NULL, 4);
}

/**
 *	@brief Function to control DAC for TCXO frequency control
 *	@param oe output enable control: 0 - output disabled, 1 - output enabled
 *	@param data pointer to DAC value (1 byte)
 */
void Control_TCXO_DAC (unsigned char oe, uint16_t *data) //controls DAC (AD5601)
{
	volatile int spirez;
	unsigned char DAC_data[3];

	Init_SPI(SPI2_DEVICE_ID, &Spi2, XSP_MASTER_OPTION | XSP_CLK_PHASE_1_OPTION | XSP_MANUAL_SSELECT_OPTION);
	spirez = XSpi_SetSlaveSelect(&Spi2, SPI2_TCXO_DAC_SS);



	if (oe == 0) //set DAC out to three-state
	{
		DAC_data[0] = 0x03; //POWER-DOWN MODE = THREE-STATE (PD[1:0]([17:16]) = 11)
		DAC_data[1] = 0x00;
		DAC_data[2] = 0x00; //LSB data

		//spirez = alt_avalon_spi_command(DAC_SPI1_BASE, SPI_NR_TCXO_DAC, 3, DAC_data, 0, NULL, 0);
		spirez = XSpi_Transfer(&Spi1, DAC_data, NULL,3);

	}
	else //enable DAC output, set new val
	{
		DAC_data[0] = 0; //POWER-DOWN MODE = NORMAL OPERATION PD[1:0]([17:16]) = 00)
		DAC_data[1] = ((*data) >>8) & 0xFF;
		DAC_data[2] = ((*data) >>0) & 0xFF;

	    /* Update cached value of trim DAC setting */
	    vctcxo_trim_dac_value = (uint16_t) *data;
		//spirez = alt_avalon_spi_command(DAC_SPI1_BASE, SPI_NR_TCXO_DAC, 3, DAC_data, 0, NULL, 0);
		spirez = XSpi_Transfer(&Spi1, DAC_data, NULL,3);
	}
}

/**
 *	@brief Function to control ADF for TCXO frequency control
 *	@param oe output enable control: 0 - output disabled, 1 - output enabled
 *	@param data pointer to ADF data block (3 bytes)
 */
void Control_TCXO_ADF (unsigned char oe, unsigned char *data) //controls ADF4002
{
	volatile int spirez;
	unsigned char ADF_data[12], ADF_block;

	Init_SPI(SPI1_DEVICE_ID, &Spi1, XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION);
	spirez = XSpi_SetSlaveSelect(&Spi1, SPI2_ADF_SS);

	if (oe == 0) //set ADF4002 CP to three-state and MUX_OUT to DGND
	{
		ADF_data[0] = 0x1f;
		ADF_data[1] = 0x81;
		ADF_data[2] = 0xf3;
		ADF_data[3] = 0x1f;
		ADF_data[4] = 0x81;
		ADF_data[5] = 0xf2;
		ADF_data[6] = 0x00;
		ADF_data[7] = 0x01;
		ADF_data[8] = 0xf4;
		ADF_data[9] = 0x01;
		ADF_data[10] = 0x80;
		ADF_data[11] = 0x01;

		//Reconfigure_SPI_for_LMS();

		//write data to ADF
		for(ADF_block = 0; ADF_block < 4; ADF_block++)
		{
			//spirez = alt_avalon_spi_command(SPI_1_ADF_BASE, SPI_NR_ADF4002, 3, &ADF_data[ADF_block*3], 0, NULL, 0);
			spirez = XSpi_Transfer(&Spi1, &ADF_data[ADF_block*3], NULL,3);
		}
	}
	else //set PLL parameters, 4 blocks must be written
	{
		//spirez = alt_avalon_spi_command(SPI_1_ADF_BASE, SPI_NR_ADF4002, 3, data, 0, NULL, 0);
		spirez = XSpi_Transfer(&Spi1, data, NULL,3);
	}
}

void ResetPLL(void)
{
	uint8_t wr_buf[4];
	uint8_t rd_buf[4];
	int pll_ind, spirez;

	// Read
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x23;	// Command and Address
	//spirez = alt_avalon_spi_command(PLLCFG_SPI_BASE, 0, 2, wr_buf, 2, rd_buf, 0);
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);

	// Get PLL index
	pll_ind = PLL_IND(rd_buf[3]); //(rd_buf[0] >> 3) & 0x3F;


	// Toggle reset line of appropriate PLL
    //IOWR(PLL_RST_BASE, 0x00, 0x01 << pll_ind);	//Set to 1
    XGpio_DiscreteWrite(&pll_rst, 1, 0x01 << pll_ind);
    asm("nop"); asm("nop");
    //IOWR(PLL_RST_BASE, 0x00, 0x00);	//Set to 0
    XGpio_DiscreteWrite(&pll_rst, 1, 0x00);

    /* PLL Software reset trough AXI interface*/
	//XGpio_DiscreteWrite(&extm_0_axi_sel, 1, pll_ind); 	// Select AXI slave
	//Xil_Out32(XPAR_EXTM_0_AXI_BASEADDR + XIL_CCR_RESET, 0x0000000A);	// Write to CCR
}

// Change PLL phase
void RdPLLCFG(tXPLL_CFG *pll_cfg)
{
	uint8_t wr_buf[4];
	uint8_t rd_buf[4];
	int spirez;

	uint8_t D_BYP, M_BYP, C0_BYP, C1_BYP, C2_BYP, C3_BYP, C4_BYP, C5_BYP, C6_BYP;

	/* Get DIV and MULT bypass values */
	/* D_BYP and M_BYP values comes from compatibility with existing Altera GW*/
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x26;	// Command and Address
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
	D_BYP = rd_buf[3] & 0x01;
	M_BYP = (rd_buf[3] >> 2) & 0x01;

	/* Get Output counter bypass values */
	/* OX_BYP values comes from compatibility with existing Altera GW*/
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x27;	// Command and Address
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
	C0_BYP = rd_buf[3] & 0x01;
	C1_BYP = (rd_buf[3] >>  2) & 0x01;
	C2_BYP = (rd_buf[3] >>  4) & 0x01;
	C3_BYP = (rd_buf[3] >>  6) & 0x01;
	C4_BYP = (rd_buf[2]) & 0x01;
	C5_BYP = (rd_buf[2] >> 2) & 0x01;
	C6_BYP = (rd_buf[2] >> 4) & 0x01;

	/* Read Divide value */
	if (D_BYP) {
		pll_cfg->DIVCLK_DIVIDE =1;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x2A;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->DIVCLK_DIVIDE	= rd_buf[2] + rd_buf[3];
	}

	/* Read Multiply value */
	if (M_BYP) {
		pll_cfg->CLKFBOUT_MULT	=1;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x2B;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->CLKFBOUT_MULT	=rd_buf[2] + rd_buf[3];
	}

	/* Read Fractional multiply part*/
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x2C;	// Command and Address
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
	pll_cfg->CLKFBOUT_FRAC	=MFRAC_CNT_LSB(rd_buf[2], rd_buf[3]);

	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x2D;	// Command and Address
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);

	pll_cfg->CLKFBOUT_FRAC	= pll_cfg->CLKFBOUT_FRAC | MFRAC_CNT_MSB(rd_buf[2], rd_buf[3]);
	pll_cfg->CLKFBOUT_PHASE	=0;

	/* Read C0 divider*/
	if (C0_BYP) {
		pll_cfg->CLKOUT0_DIVIDE  = 1;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x2E;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->CLKOUT0_DIVIDE	= rd_buf[2] + rd_buf[3];
	}


	pll_cfg->CLKOUT0_FRAC    =0;
	pll_cfg->CLKOUT0_PHASE   =0*1000;	//Phase value = (Phase Requested) * 1000. For example, for a 45.5 degree phase, the required value is 45500 = 0xB1BC.
	pll_cfg->CLKOUT0_DUTY    =50*1000; 	//Duty cycle value = (Duty Cycle in %) * 1000

	/* Read C1 divider*/
	if (C1_BYP) {
		pll_cfg->CLKOUT1_DIVIDE  = pll_cfg->CLKOUT0_DIVIDE;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x2F;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->CLKOUT1_DIVIDE	= rd_buf[2] + rd_buf[3];
	}

	pll_cfg->CLKOUT1_PHASE   =0*1000;
	pll_cfg->CLKOUT1_DUTY    =50*1000;

	/* Read C2 divider*/
	if (C2_BYP) {
		/* All register has to be set to valid values so we take same value as CO output */
		pll_cfg->CLKOUT2_DIVIDE  = pll_cfg->CLKOUT0_DIVIDE;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x30;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->CLKOUT2_DIVIDE	= rd_buf[2] + rd_buf[3];
	}

	pll_cfg->CLKOUT2_PHASE   =0;
	pll_cfg->CLKOUT2_DUTY    =50*1000;

	/* Read C3 divider*/
	if (C3_BYP) {
		/* All register has to be set to valid values so we take same value as CO output */
		pll_cfg->CLKOUT3_DIVIDE  =pll_cfg->CLKOUT0_DIVIDE;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x31;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->CLKOUT3_DIVIDE	= rd_buf[2] + rd_buf[3];
	}

	pll_cfg->CLKOUT3_PHASE   =0;
	pll_cfg->CLKOUT3_DUTY    =50*1000;

	/* Read C4 divider*/
	if (C4_BYP) {
		/* All register has to be set to valid values so we take same value as CO output */
		pll_cfg->CLKOUT4_DIVIDE  =pll_cfg->CLKOUT0_DIVIDE;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x32;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->CLKOUT4_DIVIDE	= rd_buf[2] + rd_buf[3];
	}
	pll_cfg->CLKOUT4_PHASE   =0;
	pll_cfg->CLKOUT4_DUTY    =50*1000;

	/* Read C5 divider*/
	if (C5_BYP) {
		/* All register has to be set to valid values so we take same value as CO output */
		pll_cfg->CLKOUT5_DIVIDE  =pll_cfg->CLKOUT0_DIVIDE;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x33;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->CLKOUT5_DIVIDE	= rd_buf[2] + rd_buf[3];
	}
	pll_cfg->CLKOUT5_PHASE   =0;
	pll_cfg->CLKOUT5_DUTY    =50*1000;

	/* Read C6 divider*/
	if (C6_BYP) {
		/* All register has to be set to valid values so we take same value as CO output */
		pll_cfg->CLKOUT6_DIVIDE  =pll_cfg->CLKOUT0_DIVIDE;
	}
	else{
		wr_buf[0] = 0x00;	// Command and Address
		wr_buf[1] = 0x34;	// Command and Address
		spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
		spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
		pll_cfg->CLKOUT6_DIVIDE	= rd_buf[2] + rd_buf[3];
	}
	pll_cfg->CLKOUT6_PHASE   =0;
	pll_cfg->CLKOUT6_DUTY    =50*1000;

}

// Change PLL phase
void RdTXPLLCFG(tXPLL_CFG *pll_cfg)
{
	pll_cfg->DIVCLK_DIVIDE	=1;
	pll_cfg->CLKFBOUT_MULT	=64;
	pll_cfg->CLKFBOUT_FRAC	=0;
	pll_cfg->CLKFBOUT_PHASE	=0;

	pll_cfg->CLKOUT0_DIVIDE  =64;
	pll_cfg->CLKOUT0_FRAC    =0;
	pll_cfg->CLKOUT0_PHASE   =0*1000;	//Phase value = (Phase Requested) * 1000. For example, for a 45.5 degree phase, the required value is 45500 = 0xB1BC.
	pll_cfg->CLKOUT0_DUTY    =50*1000; 	//Duty cycle value = (Duty Cycle in %) * 1000

	pll_cfg->CLKOUT1_DIVIDE  =64;
	pll_cfg->CLKOUT1_PHASE   =90*1000;
	pll_cfg->CLKOUT1_DUTY    =50*1000;

	pll_cfg->CLKOUT2_DIVIDE  =64;
	pll_cfg->CLKOUT2_PHASE   =0;
	pll_cfg->CLKOUT2_DUTY    =50*1000;

	pll_cfg->CLKOUT3_DIVIDE  =64;
	pll_cfg->CLKOUT3_PHASE   =0;
	pll_cfg->CLKOUT3_DUTY    =50*1000;

	pll_cfg->CLKOUT4_DIVIDE  =64;
	pll_cfg->CLKOUT4_PHASE   =0;
	pll_cfg->CLKOUT4_DUTY    =50*1000;

	pll_cfg->CLKOUT5_DIVIDE  =64;
	pll_cfg->CLKOUT5_PHASE   =0;
	pll_cfg->CLKOUT5_DUTY    =50*1000;

	pll_cfg->CLKOUT6_DIVIDE  =64;
	pll_cfg->CLKOUT6_PHASE   =0;
	pll_cfg->CLKOUT6_DUTY    =50*1000;

}

void RdADCPLLCFG(tXPLL_CFG *pll_cfg)
{
	pll_cfg->DIVCLK_DIVIDE	=1;
	pll_cfg->CLKFBOUT_MULT	=46;
	pll_cfg->CLKFBOUT_FRAC	=0;
	pll_cfg->CLKFBOUT_PHASE	=0;

	pll_cfg->CLKOUT0_DIVIDE  =46;
	pll_cfg->CLKOUT0_FRAC    =0;
	pll_cfg->CLKOUT0_PHASE   =-10*1000;	//Phase value = (Phase Requested) * 1000. For example, for a 45.5 degree phase, the required value is 45500 = 0xB1BC.
	pll_cfg->CLKOUT0_DUTY    =50*1000; 	//Duty cycle value = (Duty Cycle in %) * 1000

	pll_cfg->CLKOUT1_DIVIDE  =46;
	pll_cfg->CLKOUT1_PHASE   =360*1000;
	pll_cfg->CLKOUT1_DUTY    =50*1000;

	pll_cfg->CLKOUT2_DIVIDE  =46;
	pll_cfg->CLKOUT2_PHASE   =0;
	pll_cfg->CLKOUT2_DUTY    =50*1000;

	pll_cfg->CLKOUT3_DIVIDE  =46;
	pll_cfg->CLKOUT3_PHASE   =0;
	pll_cfg->CLKOUT3_DUTY    =50*1000;

	pll_cfg->CLKOUT4_DIVIDE  =46;
	pll_cfg->CLKOUT4_PHASE   =0;
	pll_cfg->CLKOUT4_DUTY    =50*1000;

	pll_cfg->CLKOUT5_DIVIDE  =46;
	pll_cfg->CLKOUT5_PHASE   =0;
	pll_cfg->CLKOUT5_DUTY    =50*1000;

	pll_cfg->CLKOUT6_DIVIDE  =46;
	pll_cfg->CLKOUT6_PHASE   =0;
	pll_cfg->CLKOUT6_DUTY    =50*1000;

}




// Change PLL phase
uint8_t UpdatePHCFG(void)
{
	uint32_t PLL_BASE;
	uint32_t Val, Cx, Dir;
	uint8_t wr_buf[4];
	uint8_t rd_buf[4];
	int pll_ind, spirez;
	uint8_t pllcfgrez;
	tXPLL_CFG pll_cfg = {0};

	// Read
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x23;	// Command and Address
	//spirez = alt_avalon_spi_command(PLLCFG_SPI_BASE, 0, 2, wr_buf, 2, rd_buf, 0);
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);

	// Get PLL base address
	//PLL_BASE = GetPLLCFG_Base( PLL_IND(rd_buf[1]) );

	// Get PLL index
	pll_ind = PLL_IND(rd_buf[3]); //(rd_buf[0] >> 3) & 0x3F;

    /* PLL Software reset trough AXI interface*/
	XGpio_DiscreteWrite(&extm_0_axi_sel, 1, 1); 					// Select PLL AXI slave
	Xil_Out32(XPAR_EXTM_0_AXI_BASEADDR + XIL_CCR_RESET, 0x0000000A);	// Write 0x0000000A value to reset PLL

	//Write in Mode Register "0" for waitrequest mode, "1" for polling mode
	//IOWR_32DIRECT(PLL_BASE, MODE, 0x01);

	// Set Up/Down
	Dir = PH_DIR(rd_buf[0]); //(rd_buf[1] >> 5) & 0x01;

	// Set Cx
	Cx = CX_IND(rd_buf[0]) - 2; //(rd_buf[1] & 0x1F);

	// Set Phase Cnt
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x24;	// Command and Address
	//spirez = alt_avalon_spi_command(PLLCFG_SPI_BASE, 0, 2, wr_buf, 2, rd_buf, 0);
	Val = CX_PHASE(rd_buf[0], rd_buf[1]); //(rd_buf[1] << 8) | rd_buf[0];

	// Set Phase shift register
	//set_Phase(PLL_BASE, Cx, Val, Dir);
	// Apply PLL configuration
	//pllcfgrez = start_Reconfig(PLL_BASE);

	switch(pll_ind) {
	case 0:
		RdRXPLLCFG(&pll_cfg);
		break;
	case 1:
		RdRXPLLCFG(&pll_cfg);
		break;
	case 2:
		RdRXPLLCFG(&pll_cfg);
		break;
	case 3:
		RdRXPLLCFG(&pll_cfg);
		break;
	default:
		RdRXPLLCFG(&pll_cfg);
		break;
	}




	// Update PLL configuration;
	pllcfgrez = set_xpll_config(XPAR_EXTM_0_AXI_BASEADDR, &pll_cfg);

	// Apply PLL configuration
	pllcfgrez = start_XReconfig(XPAR_EXTM_0_AXI_BASEADDR);

	Xil_Out32(XPAR_EXTM_0_AXI_BASEADDR + XIL_CCR_RESET, 0x0000000A);	// Write 0x0000000A value to reset PLL

	for (int i=0; i<3600; i++){
		pll_cfg.CLKOUT1_PHASE = i*100;
		pllcfgrez = set_xpll_config(XPAR_EXTM_0_AXI_BASEADDR, &pll_cfg);
		pllcfgrez = start_XReconfig(XPAR_EXTM_0_AXI_BASEADDR);
	}



	return pllcfgrez;
}

int CheckSamples(int sel) {

	int cmp_status = 1;
	int timeout;

	/* Select sample compare MUX */
	XGpio_DiscreteWrite(&smpl_cmp_sel, 1, sel);
	/* Disable sample compare*/
	XGpio_DiscreteWrite(&smpl_cmp_en, 1, 0x00);


	timeout = 0;
	do {
		cmp_status = XGpio_DiscreteRead(&smpl_cmp_status, 1);
		if (timeout++ > PLLCFG_TIMEOUT) return 0;
	}
	while((cmp_status & 0x01)!= 0);

	/* Enalbe sample compare */
	XGpio_DiscreteWrite(&smpl_cmp_en, 1, 0x01);

	timeout = 0;
	do {
		cmp_status = XGpio_DiscreteRead(&smpl_cmp_status, 1);
		if (timeout++ > PLLCFG_TIMEOUT) return 0;
	}
	while((cmp_status & 0x01)== 0);

	return cmp_status;
}

// Change PLL phase
uint8_t AutoUpdatePHCFG(void)
{
	uint32_t PLL_BASE;
	uint32_t Val, Cx, Dir;
	uint8_t wr_buf[4];
	uint8_t rd_buf[4];
	int pll_ind, spirez;
	uint8_t pllcfgrez;
	tXPLL_CFG pll_cfg = {0};
	int PhaseMin = 0;
	int PhaseMax = 0;
	int PhaseMiddle = 0;
	int PhaseRange = 0;
	int cmp_status = 0;
	int cmp_status_1 = 0;
	int cmp_en = 0;
	int cmp_sel= 0;
	int timeout;
	int lock_status;

	/* State machine for VCTCXO tuning */
	typedef enum state {
	    PHASE_MIN,
	    PHASE_MAX,
	    PHASE_DONE,
	    DO_NOTHING
	} state_t;

	state_t phase_state = PHASE_MIN;

	// Read
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x23;	// Command and Address
	//spirez = alt_avalon_spi_command(PLLCFG_SPI_BASE, 0, 2, wr_buf, 2, rd_buf, 0);
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);

	// Get PLL index
	pll_ind = PLL_IND(rd_buf[3]); //(rd_buf[0] >> 3) & 0x3F;


	// Set Up/Down
	//Dir = PH_DIR(rd_buf[0]); //(rd_buf[1] >> 5) & 0x01;

	// Set Cx
	//Cx = CX_IND(rd_buf[0]) - 2; //(rd_buf[1] & 0x1F);

	// Set Phase Cnt
	//wr_buf[0] = 0x00;	// Command and Address
	//wr_buf[1] = 0x24;	// Command and Address
	//spirez = alt_avalon_spi_command(PLLCFG_SPI_BASE, 0, 2, wr_buf, 2, rd_buf, 0);
	//Val = CX_PHASE(rd_buf[0], rd_buf[1]); //(rd_buf[1] << 8) | rd_buf[0];

	RdPLLCFG(&pll_cfg);

	switch(pll_ind) {
	case 0:
		cmp_sel = 0x00;
		break;
	case 1:
		cmp_sel = 0x00;
		break;
	case 2:
		cmp_sel = 0x01;
		break;
	case 3:
		cmp_sel = 0x01;
		break;
	default:
		cmp_sel= 0x00;
		break;
	}

	/* Select sample compare MUX */
	XGpio_DiscreteWrite(&smpl_cmp_sel, 1, cmp_sel);
	XGpio_DiscreteWrite(&smpl_cmp_en, 1, 0x00);



	XGpio_DiscreteWrite(&extm_0_axi_sel, 1, pll_ind); 					// Select PLL AXI slave
	pll_cfg.CLKOUT0_PHASE = 0;
	pll_cfg.CLKOUT1_PHASE = 0;

	pllcfgrez = set_xpll_config(XPAR_EXTM_0_AXI_BASEADDR, &pll_cfg);
	pllcfgrez = start_XReconfig(XPAR_EXTM_0_AXI_BASEADDR);

	for (int i=0; i<360; i++){

		timeout = 0;
		do
		{
			lock_status = Xil_In32(XPAR_EXTM_0_AXI_BASEADDR + XIL_CCR_STATUS);
		  	if (timeout++ > PLLCFG_TIMEOUT) return PHCFG_ERROR;
		}
		while (!(lock_status & 0x01));

		cmp_status 	= CheckSamples(cmp_sel);


		switch(phase_state) {
		case PHASE_MIN:
			if (cmp_status == 0x01) {
				phase_state = PHASE_MAX;
				PhaseMin = i;
			}
			break;
		case PHASE_MAX:
			if (cmp_status == 0x03) {
				PhaseMax = i;
				PhaseRange = (PhaseMax - PhaseMin);
				PhaseMiddle = PhaseMin + (PhaseMax - PhaseMin) / 2;
				if (PhaseRange > 10) {
					phase_state = PHASE_DONE;
				}
				else
				{
					phase_state = PHASE_MIN;
				}
			}
			break;
		case PHASE_DONE:
			break;

		default:
			break;

		}

		if (phase_state != PHASE_DONE) {
			XGpio_DiscreteWrite(&smpl_cmp_en, 1, 0x00);
		    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
			do {
				cmp_status = XGpio_DiscreteRead(&smpl_cmp_status, 1);
			}
			while((cmp_status & 0x01)!= 0);
			pll_cfg.CLKOUT1_PHASE = i*1000;
			while(!(Xil_In32(XPAR_EXTM_0_AXI_BASEADDR + XIL_CCR_STATUS)));
			pllcfgrez = set_xpll_config(XPAR_EXTM_0_AXI_BASEADDR, &pll_cfg);
			pllcfgrez = start_XReconfig(XPAR_EXTM_0_AXI_BASEADDR);

		}

		else {
			XGpio_DiscreteWrite(&smpl_cmp_en, 1, 0x00);
		    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
			do {
				cmp_status = XGpio_DiscreteRead(&smpl_cmp_status, 1);
			}
			while((cmp_status & 0x01)!= 0);
			pll_cfg.CLKOUT1_PHASE = PhaseMiddle*1000;
			while(!(Xil_In32(XPAR_EXTM_0_AXI_BASEADDR + XIL_CCR_STATUS)));
			pllcfgrez = set_xpll_config(XPAR_EXTM_0_AXI_BASEADDR, &pll_cfg);
			pllcfgrez = start_XReconfig(XPAR_EXTM_0_AXI_BASEADDR);
			return PHCFG_DONE;
			break;
		}

	}



	return PHCFG_ERROR;
}

// Updates PLL configuration
uint8_t UpdatePLLCFG(void)
{
	uint8_t wr_buf[4];
	uint8_t rd_buf[4];
	int pll_ind, spirez;
	uint8_t pllcfgrez;
	tXPLL_CFG pll_cfg = {0};

	// Get PLL index
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x23;	// Command and Address
	//spirez = alt_avalon_spi_command(PLLCFG_SPI_BASE, 0, 2, wr_buf, 2, rd_buf, 0);
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
	pll_ind = PLL_IND(rd_buf[3]); //(rd_buf[0] >> 3) & 0x3F;

	// Select PLL AXI slave to which PLL is connected externally
	XGpio_DiscreteWrite(&extm_0_axi_sel, 1, pll_ind);

	RdPLLCFG(&pll_cfg);

	pllcfgrez = set_xpll_config(XPAR_EXTM_0_AXI_BASEADDR, &pll_cfg);
	pllcfgrez = start_XReconfig(XPAR_EXTM_0_AXI_BASEADDR);

	return pllcfgrez;

}

// Updates PLL configuration
uint8_t UpdateADCPLLCFG(void)
{
	uint8_t wr_buf[4];
	uint8_t rd_buf[4];
	int pll_ind, spirez;
	uint8_t pllcfgrez;
	tXPLL_CFG pll_cfg = {0};

	// Get PLL index
	wr_buf[0] = 0x00;	// Command and Address
	wr_buf[1] = 0x23;	// Command and Address
	//spirez = alt_avalon_spi_command(PLLCFG_SPI_BASE, 0, 2, wr_buf, 2, rd_buf, 0);
	spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
	spirez = XSpi_Transfer(&Spi0, wr_buf, rd_buf, 4);
	pll_ind = 4; //(rd_buf[0] >> 3) & 0x3F;

	// Select PLL AXI slave to which PLL is connected externally
	XGpio_DiscreteWrite(&extm_0_axi_sel, 1, pll_ind);

	RdADCPLLCFG(&pll_cfg);

	pllcfgrez = set_xpll_config(XPAR_EXTM_0_AXI_BASEADDR, &pll_cfg);
	pllcfgrez = start_XReconfig(XPAR_EXTM_0_AXI_BASEADDR);

	return pllcfgrez;

}

void CfgPLL(void)
{
	int pll_stat;

	pll_stat = Xil_In16(XPAR_EXTM_0_AXI_BASEADDR + CLK_CONFIG);

	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + POWER 			, 0xFFFF);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT0_REG1	, 0x1104);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT0_REG2	, 0x0000);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT1_REG1	, 0x1104);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT1_REG2	, 0x0100);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT2_REG1	, 0x1041);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT2_REG2	, 0x00c0);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT3_REG1	, 0x1041);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT3_REG2	, 0x00c0);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT4_REG1	, 0x1041);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT4_REG2	, 0x00c0);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT5_REG1	, 0x1041);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT5_REG2	, 0x00c0);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT6_REG1	, 0x1041);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKOUT6_REG2	, 0x00c0);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + DIVCLK			, 0x1041);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKFBOUT_REG1	, 0x1104);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLKFBOUT_REG2	, 0x0000);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + LOCK_REG1		, 0x01e8);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + LOCK_REG2		, 0x5801);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + LOCK_REG3		, 0x59e9);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + FILTER_REG1	, 0x0800);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + FILTER_REG2	, 0x0900);
	Xil_Out16(XPAR_EXTM_0_AXI_BASEADDR + CLK_CONFIG		, 0x0003);

	//while (Xil_In16(XPAR_EXTM_0_AXI_BASEADDR + CLK_CONFIG) == 3 )

	pll_stat = Xil_In16(XPAR_EXTM_0_AXI_BASEADDR + CLK_CONFIG);

}

/*****************************************************************************/
/**
*
* This is the Wait_For_Lock function, it will wait for lock to settle change
* frequency value
*
* @param	CfgPtr_Dynamic provides pointer to clock wizard dynamic config
*
* @return
*		- Error 0 for pass scenario
*		- Error > 0 for failure scenario
*
* @note		None
*
******************************************************************************/
int Wait_For_Lock(XClk_Wiz_Config *CfgPtr_Dynamic)
{
	u32 Count = 0;
	u32 Error = 0;

	while(!(*(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x04) & CLK_LOCK)) {
		if(Count == 10000) {
			Error++;
			break;
		}
		Count++;
        }
    return Error;
}

/*****************************************************************************/
/**
*
* This is the Clk_Wiz_Reconfig function, it will reconfigure frequencies as
* per input array
*
* @param	CfgPtr_Dynamic provides pointer to clock wizard dynamic config
* @param	Findex provides the index for Frequency divide register
* @param	Sindex provides the index for Frequency phase register
*
* @return
*		-  Error 0 for pass scenario
*		-  Error > 0 for failure scenario
*
* @note	 None
*
******************************************************************************/
int Clk_Wiz_Reconfig(XClk_Wiz_Config *CfgPtr_Dynamic)
{
    u32 Count = 0;
    u32 Error = 0;
    u32 Fail  = 0;
    u32 Frac_en = 0;
    u32 Frac_divide = 0;
    u32 Divide = 0;
    float Freq = 0.0;

    Fail = Wait_For_Lock(CfgPtr_Dynamic);
    if(Fail) {
	Error++;
        //xil_printf("\n ERROR: Clock is not locked for default frequency" \
	" : 0x%x\n\r", *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x04) & CLK_LOCK);
     }

    /* SW reset applied */
    *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x00) = 0xA;


    if(*(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x04) & CLK_LOCK) {
	Error++;
       // xil_printf("\n ERROR: Clock is locked : 0x%x \t expected "\
	  "0x00\n\r", *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x04) & CLK_LOCK);
    }

    /* Wait cycles after SW reset */
    for(Count = 0; Count < 2000; Count++);

    Fail = Wait_For_Lock(CfgPtr_Dynamic);
    if(Fail) {
	  Error++;
          //xil_printf("\n ERROR: Clock is not locked after SW reset :"
	     // "0x%x \t Expected  : 0x1\n\r",
	      //*(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x04) & CLK_LOCK);
    }

    /* Calculation of Input Freq and Divide factors*/
    Freq = ((float) CLK_WIZ_VCO_FACTOR/ DYNAMIC_INPUT_FREQ_FACTOR);

    Divide = Freq;
    Freq = (float)(Freq - Divide);

    Frac_divide = Freq * 10000;

    if(Frac_divide % 10 > 5) {
	   Frac_divide = Frac_divide + 10;
    }
    Frac_divide = Frac_divide/10;

    if(Frac_divide > 1023 ) {
	   Frac_divide = Frac_divide / 10;
    }

    if(Frac_divide) {
	   /* if fraction part exists, Frac_en is shifted to 26
	    * for input Freq */
	   Frac_en = (CLK_FRAC_EN << 26);
    }
    else {
	   Frac_en = 0;
    }

    /* Configuring Multiply and Divide values */
    *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x200) = \
	Frac_en | (Frac_divide << 16) | (Divide << 8) | 0x01;
    *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x204) = 0x00;

    /* Calculation of Output Freq and Divide factors*/
    Freq = ((float) CLK_WIZ_VCO_FACTOR / DYNAMIC_OUTPUT_FREQFACTOR);

    Divide = Freq;
    Freq = (float)(Freq - Divide);

    Frac_divide = Freq * 10000;

    if(Frac_divide%10 > 5) {
	Frac_divide = Frac_divide + 10;
    }
    Frac_divide = Frac_divide / 10;

    if(Frac_divide > 1023 ) {
        Frac_divide = Frac_divide / 10;
    }

    if(Frac_divide) {
	/* if fraction part exists, Frac_en is shifted to 18 for output Freq */
	Frac_en = (CLK_FRAC_EN << 18);
    }
    else {
	Frac_en = 0;
    }

    /* Configuring Multiply and Divide values */
    *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x208) =
	    Frac_en | (Frac_divide << 8) | (Divide);
    *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x20C) = 0x00;

    /* Load Clock Configuration Register values */
    *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x25C) = 0x07;

    if(*(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x04) & CLK_LOCK) {
	Error++;
       // xil_printf("\n ERROR: Clock is locked : 0x%x \t expected "
	   // "0x00\n\r", *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x04) & CLK_LOCK);
     }

     /* Clock Configuration Registers are used for dynamic reconfiguration */
     *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x25C) = 0x02;

    Fail = Wait_For_Lock(CfgPtr_Dynamic);
    if(Fail) {
	Error++;
        //xil_printf("\n ERROR: Clock is not locked : 0x%x \t Expected "\
	": 0x1\n\r", *(u32 *)(CfgPtr_Dynamic->BaseAddr + 0x04) & CLK_LOCK);
    }
	return Error;
}

int RdADC(uint8_t ch) {

	uint8_t wr_buf[3];
	uint8_t rd_buf[3];
	uint16_t D;
	int64_t Vin;
	int64_t Vref = 2500000; // Reference voltage in uV
	int spirez;

	/* Set SPI 1 0 mode */
	spirez = XSpi_SetOptions(&Spi1, XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION);

	// Select ADC on SPI1
	spirez = XSpi_SetSlaveSelect(&Spi1, SPI1_ADC_SS);

	// Disable ADC readout and reset
	wr_buf[0] = 0x06;		// Start bit, Input cofig: Single-ended,
	wr_buf[1] = ch << 6;	// Channel selection
	wr_buf[2] = 0x00;		// Dont care
	spirez = XSpi_Transfer(&Spi1, wr_buf, rd_buf, 3);

	D = ((rd_buf[1] & 0x0F) << 8) | rd_buf[2];

	Vin = D * Vref / 4096;

	// Return measured voltage Vin in uV
	return Vin;
}


int main()
{
	uint8_t wr_buf[4];
	uint8_t rd_buf[4];

    uint8_t vctcxo_tamer_irq = 0;
    uint8_t vctcxo_tamer_en=0,	vctcxo_tamer_en_old = 0;

    // Trim DAC constants
    const uint16_t trimdac_min       = 0x1938; // Decimal value = 6456
    const uint16_t trimdac_max       = 0xE2F3; // Decimal value = 58099

    // Trim DAC calibration line
    line_t trimdac_cal_line;

    // VCTCXO Tune State machine
    state_t tune_state = COARSE_TUNE_MIN;

    // Set the known/default values of the trim DAC cal line
    trimdac_cal_line.point[0].x  = 0;
    trimdac_cal_line.point[0].y  = trimdac_min;
    trimdac_cal_line.point[1].x  = 0;
    trimdac_cal_line.point[1].y  = trimdac_max;
    trimdac_cal_line.slope       = 0;
    trimdac_cal_line.y_intercept = 0;
    struct vctcxo_tamer_pkt_buf vctcxo_tamer_pkt;
    vctcxo_tamer_pkt.ready = false;

	uint8_t phcfg_start_old, phcfg_start;
	uint8_t pllcfg_start_old, pllcfg_start;
	uint8_t pllrst_start_old, pllrst_start;
	uint8_t phcfg_mode;
tXPLL_CFG pll_cfg = {0};
	uint8_t pllcfgrez;
	uint16_t phcfgrez;

	u32 pll_stat;

	u32 Fail  = 0;

	XClk_Wiz_Config *CfgPtr_Mon;
	XClk_Wiz_Config *CfgPtr_Dynamic;
	u32 Status = XST_SUCCESS;

	int spirez;
	uint32_t* dest = (uint32_t*)glEp0Buffer_Tx;
	u8 spi_WriteBuffer[4];
	u8 spi_ReadBuffer[4];
	int spi_ByteCount;
	u8 Iic_WriteBuffer[2];
	u8 Iic_ReadBuffer[2];
	int ByteCount;

    init_platform();

    //initialize XGpio variable
    XGpio_Initialize(&gpio, XPAR_AXI_GPIO_0_DEVICE_ID);
    XGpio_Initialize(&pll_rst, XPAR_PLL_RST_DEVICE_ID);
    XGpio_Initialize(&pllcfg_cmd, XPAR_PLLCFG_COMMAND_DEVICE_ID);
    XGpio_Initialize(&pllcfg_stat, XPAR_PLLCFG_STATUS_DEVICE_ID);
    XGpio_Initialize(&extm_0_axi_sel, XPAR_EXTM_0_AXI_SEL_DEVICE_ID);
    XGpio_Initialize(&smpl_cmp_sel, XPAR_SMPL_CMP_SEL_DEVICE_ID);
    XGpio_Initialize(&smpl_cmp_en, XPAR_SMPL_CMP_CMD_DEVICE_ID);
    XGpio_Initialize(&smpl_cmp_status, XPAR_SMPL_CMP_STAT_DEVICE_ID);
    XGpio_Initialize(&vctcxo_tamer_ctrl, XPAR_VCTCXO_TAMER_CTRL_DEVICE_ID);



	// Initialize variables to detect PLL phase change and PLL config update request
	phcfg_start_old = 0; phcfg_start = 0;
	pllcfg_start_old = 0; pllcfg_start = 0;
	pllrst_start_old = 0; pllrst_start = 0;

    Init_SPI(SPI0_DEVICE_ID, &Spi0, XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION);
    Init_SPI(SPI1_DEVICE_ID, &Spi1, XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION);
    Init_SPI(SPI2_DEVICE_ID, &Spi2, XSP_MASTER_OPTION | XSP_CLK_PHASE_1_OPTION | XSP_MANUAL_SSELECT_OPTION);


    // TODO: implement different configurations
    Init_CDCM(&Spi1, 1);
    Init_CDCM(&Spi1, 2);

    Control_TCXO_ADF (0, NULL);		//set ADF4002 CP to three-state
	dac_val = 30714;
	Control_TCXO_DAC (1, &dac_val); //enable DAC output, set new val

	//UpdateADCPLLCFG();

	// Initialize ADC
	init_ADC(&Spi1, SPI1_LMS2_BB_ADC1_SS);
	init_ADC(&Spi1, SPI1_LMS2_BB_ADC2_SS);
	init_ADC(&Spi1, SPI1_LMS3_BB_ADC1_SS);
	init_ADC(&Spi1, SPI1_LMS3_BB_ADC2_SS);
	//TestMode_ADC();


	//pllcfgrez = AutoUpdatePHCFG();

	RdPLLCFG(&pll_cfg);
	//pllcfgrez = UpdatePLLCFG();


	// ADC test read
	int test_rd;
	test_rd = RdADC(0x00);
	test_rd = RdADC(0x01);
	test_rd = RdADC(0x02);
	test_rd = RdADC(0x03);










    while (1)
    {
    	vctcxo_tamer_irq = (XGpio_DiscreteRead(&vctcxo_tamer_ctrl, 1) & 0x02);
	    // Clear VCTCXO tamer interrupt
	    if(vctcxo_tamer_irq != 0)
	    {
	    	vctcxo_tamer_isr(&vctcxo_tamer_pkt);
	    }

    	//Get vctcxo tamer enable bit status
    	vctcxo_tamer_en_old = vctcxo_tamer_en;
    	vctcxo_tamer_en = (XGpio_DiscreteRead(&vctcxo_tamer_ctrl, 1) & 0x01);

    	if (vctcxo_tamer_en_old != vctcxo_tamer_en){
    		if (vctcxo_tamer_en == 0x01){
    			vctcxo_tamer_init();
    			vctcxo_tamer_pkt.ready = true;
    		}
    		else {
    			vctcxo_tamer_dis();
    			tune_state = COARSE_TUNE_MIN;
    			vctcxo_tamer_pkt.ready = false;
    		}
    	}


        /* Temporarily putting the VCTCXO Calibration stuff here. */
        if( vctcxo_tamer_pkt.ready ) {

            vctcxo_tamer_pkt.ready = false;

            switch(tune_state) {

            case COARSE_TUNE_MIN:

                /* Tune to the minimum DAC value */
                vctcxo_trim_dac_write( 0x08, trimdac_min );
                dac_val = (uint16_t) trimdac_min;
            	Control_TCXO_DAC (1, &dac_val); //enable DAC output, set new val

                /* State to enter upon the next interrupt */
                tune_state = COARSE_TUNE_MAX;
                //printf("COARSE_TUNE_MIN: \r\n\t");
                //printf("DAC value: ");
                //printf("%u;\t", (unsigned int)dac_val);


                break;

            case COARSE_TUNE_MAX:

                /* We have the error from the minimum DAC setting, store it
                 * as the 'x' coordinate for the first point */
                trimdac_cal_line.point[0].x = vctcxo_tamer_pkt.pps_1s_error;

                //printf("1s_error: ");
                //printf("%i;\r\n", (int)vctcxo_tamer_pkt.pps_1s_error);

                /* Tune to the maximum DAC value */
                vctcxo_trim_dac_write( 0x08, trimdac_max );
                dac_val = (uint16_t) trimdac_max;
            	Control_TCXO_DAC (1, &dac_val); //enable DAC output, set new val

                /* State to enter upon the next interrupt */
                tune_state = COARSE_TUNE_DONE;
                //printf("COARSE_TUNE_MAX: \r\n\t");
                //printf("DAC value: ");
                //printf("%u;\t", (unsigned int)dac_val);

                break;

            case COARSE_TUNE_DONE:
            	/* Write status to to state register*/
            	vctcxo_tamer_write(VT_STATE_ADDR, 0x01);

                /* We have the error from the maximum DAC setting, store it
                 * as the 'x' coordinate for the second point */
                trimdac_cal_line.point[1].x = vctcxo_tamer_pkt.pps_1s_error;

                //printf("1s_error: ");
                //printf("%i;\r\n", (int)vctcxo_tamer_pkt.pps_1s_error);

                /* We now have two points, so we can calculate the equation
                 * for a line plotted with DAC counts on the Y axis and
                 * error on the X axis. We want a PPM of zero, which ideally
                 * corresponds to the y-intercept of the line. */


                trimdac_cal_line.slope = ( (float) (trimdac_cal_line.point[1].y - trimdac_cal_line.point[0].y) / (float)
                                           (trimdac_cal_line.point[1].x - trimdac_cal_line.point[0].x) );

                trimdac_cal_line.y_intercept = ( trimdac_cal_line.point[0].y -
                                                 (uint16_t)(lroundf(trimdac_cal_line.slope * (float) trimdac_cal_line.point[0].x)));

                /* Set the trim DAC count to the y-intercept */
                vctcxo_trim_dac_write( 0x08, trimdac_cal_line.y_intercept );
                dac_val = (uint16_t) trimdac_cal_line.y_intercept;
            	Control_TCXO_DAC (1, &dac_val); //enable DAC output, set new val

                /* State to enter upon the next interrupt */
                tune_state = FINE_TUNE;
                //printf("COARSE_TUNE_DONE: \r\n\t");
                //printf("DAC value: ");
                //printf("%u;\r\n\r\n", (unsigned int)dac_val);
                //printf("FINE_TUNE: \r\n");
                //printf("Err_Flag;DAC value;Error;\r\n");

                break;

            case FINE_TUNE:

                /* We should be extremely close to a perfectly tuned
                 * VCTCXO, but some minor adjustments need to be made */

                /* Check the magnitude of the errors starting with the
                 * one second count. If an error is greater than the maximum
                 * tolerated error, adjust the trim DAC by the error (Hz)
                 * multiplied by the slope (in counts/Hz) and scale the
                 * result by the precision interval (e.g. 1s, 10s, 100s). */

                if( vctcxo_tamer_pkt.pps_1s_error_flag ) {
                	vctcxo_trim_dac_value = (vctcxo_trim_dac_value -
                	                    		(uint16_t) (lroundf((float)vctcxo_tamer_pkt.pps_1s_error * trimdac_cal_line.slope)/1));
                	// Write tuned val to VCTCXO_tamer MM registers
                    vctcxo_trim_dac_write( 0x08, vctcxo_trim_dac_value);
                    // Change DAC value
                    dac_val = (uint16_t) vctcxo_trim_dac_value;
                	Control_TCXO_DAC (1, &dac_val); //enable DAC output, set new val
                	//printf("001;");
                	//printf("%u;", (unsigned int)dac_val);
                	//printf("%i;\r\n", (int) vctcxo_tamer_pkt.pps_1s_error);

                } else if( vctcxo_tamer_pkt.pps_10s_error_flag ) {
                	vctcxo_trim_dac_value = (vctcxo_trim_dac_value -
                    							(uint16_t)(lroundf((float)vctcxo_tamer_pkt.pps_10s_error * trimdac_cal_line.slope)/10));
                	// Write tuned val to VCTCXO_tamer MM registers
                    vctcxo_trim_dac_write( 0x08, vctcxo_trim_dac_value);
                    // Change DAC value
                    dac_val = (uint16_t) vctcxo_trim_dac_value;
                	Control_TCXO_DAC (1, &dac_val); //enable DAC output, set new val
                	//printf("010;");
                	//printf("%u;", (unsigned int)dac_val);
                	//printf("%i;\r\n", (int) vctcxo_tamer_pkt.pps_10s_error);

                } else if( vctcxo_tamer_pkt.pps_100s_error_flag ) {
                	vctcxo_trim_dac_value = (vctcxo_trim_dac_value -
                    							(uint16_t)(lroundf((float)vctcxo_tamer_pkt.pps_100s_error * trimdac_cal_line.slope)/100));
                	// Write tuned val to VCTCXO_tamer MM registers
                    vctcxo_trim_dac_write( 0x08, vctcxo_trim_dac_value);
                    // Change DAC value
                    dac_val = (uint16_t) vctcxo_trim_dac_value;
                	Control_TCXO_DAC (1, &dac_val); //enable DAC output, set new val
                	//printf("100;");
                	//printf("%u;", (unsigned int)dac_val);
                	//printf("%i;\r\n", (int) vctcxo_tamer_pkt.pps_100s_error);
                }

                break;

            default:
                break;

            } /* switch */

            /* Take PPS counters out of reset */
            vctcxo_tamer_reset_counters( false );

            /* Enable interrupts */
            vctcxo_tamer_enable_isr( true );

        } /* VCTCXO Tamer interrupt */



	    // Check if there is a request for PLL phase update
 	    if((phcfg_start_old == 0) && (phcfg_start != 0))
	    {
	    	//IOWR(PLLCFG_STATUS_BASE, 0x00, PLLCFG_BUSY);
	    	//XGpio_DiscreteWrite(&pllcfg_stat, 1, PLLCFG_BUSY);
	    	//phcfg_mode = (IORD(PLLCFG_COMMAND_BASE, 0x00) & 0x08) >> 3;
	    	phcfg_mode = (XGpio_DiscreteRead(&pllcfg_cmd, 1) & 0x08) >> 3;
	    	if (phcfg_mode){
	    			phcfgrez = AutoUpdatePHCFG();


	    	}
	    	else{
	    			phcfgrez = 0x01;

	    	};

	    	//IOWR(PLLCFG_STATUS_BASE, 0x00, (pllcfgrez << 2) | PLLCFG_DONE);
	    	//pllcfgrez = 0x00;
	    	//XGpio_DiscreteWrite(&pllcfg_stat, 1 , (pllcfgrez << 2) | PLLCFG_DONE);
	    	//XGpio_DiscreteWrite(&pllcfg_stat, 1 , PLLCFG_DONE | PHCFG_DONE);
	    	XGpio_DiscreteWrite(&pllcfg_stat, 1 , (phcfgrez << 10) | PLLCFG_DONE);
	    }

	    // Check if there is a request for PLL configuration update
	    if((pllcfg_start_old == 0) && (pllcfg_start != 0))
	    {
	    	//IOWR(PLLCFG_STATUS_BASE, 0x00, PLLCFG_BUSY);
	    	XGpio_DiscreteWrite(&pllcfg_stat, 1, PLLCFG_BUSY);
	    	pllcfgrez = UpdatePLLCFG();
	    	//IOWR(PLLCFG_STATUS_BASE, 0x00, (pllcfgrez << 2) | PLLCFG_DONE);
	    	pllcfgrez = 0x00;
	    	//XGpio_DiscreteWrite(&pllcfg_stat, 1 , (pllcfgrez << 2) | PLLCFG_DONE);
	    	XGpio_DiscreteWrite(&pllcfg_stat, 1 , PLLCFG_DONE);
	    }

	    // Check if there is a request for PLL configuration update
	    if((pllrst_start_old == 0) && (pllrst_start != 0))
	    {
	    	//IOWR(PLLCFG_STATUS_BASE, 0x00, PLLCFG_BUSY);
	    	//XGpio_DiscreteWrite(&pllcfg_stat, 1, PLLCFG_BUSY);
	    	ResetPLL();
	    	//IOWR(PLLCFG_STATUS_BASE, 0x00, PLLCFG_DONE);
	    	XGpio_DiscreteWrite(&pllcfg_stat, 1,  PLLCFG_DONE);
	    }

	    // Update PLL configuration command status
	    pllrst_start_old = pllrst_start;
	    //pllrst_start = (IORD(PLLCFG_COMMAND_BASE, 0x00) & 0x04) >> 2;
	    pllrst_start = (XGpio_DiscreteRead(&pllcfg_cmd, 1) & 0x04) >> 2;
	    phcfg_start_old = phcfg_start;
	    //phcfg_start = (IORD(PLLCFG_COMMAND_BASE, 0x00) & 0x02) >> 1;
	    phcfg_start = (XGpio_DiscreteRead(&pllcfg_cmd, 1) & 0x02) >> 1;
	    pllcfg_start_old = pllcfg_start;
	    //pllcfg_start = IORD(PLLCFG_COMMAND_BASE, 0x00) & 0x01;
	    pllcfg_start = XGpio_DiscreteRead(&pllcfg_cmd, 1) & 0x01;

    	// Read FIFO Status
    	spirez = AXI_TO_NATIVE_FIFO_mReadReg(XPAR_AXI_TO_NATIVE_FIFO_0_S00_AXI_BASEADDR, AXI_TO_NATIVE_FIFO_S00_AXI_SLV_REG2_OFFSET);

        if(!(spirez & 0x01))
        {
        	//Toggle FIFO reset
        	AXI_TO_NATIVE_FIFO_mWriteReg(XPAR_AXI_TO_NATIVE_FIFO_0_S00_AXI_BASEADDR, AXI_TO_NATIVE_FIFO_S00_AXI_SLV_REG3_OFFSET, 0x01);
        	AXI_TO_NATIVE_FIFO_mWriteReg(XPAR_AXI_TO_NATIVE_FIFO_0_S00_AXI_BASEADDR, AXI_TO_NATIVE_FIFO_S00_AXI_SLV_REG3_OFFSET, 0x00);

        	getFifoData(glEp0Buffer_Rx, 64);

         	memset (glEp0Buffer_Tx, 0, sizeof(glEp0Buffer_Tx)); //fill whole tx buffer with zeros
         	cmd_errors = 0;

     		LMS_Ctrl_Packet_Tx->Header.Command = LMS_Ctrl_Packet_Rx->Header.Command;
     		LMS_Ctrl_Packet_Tx->Header.Data_blocks = LMS_Ctrl_Packet_Rx->Header.Data_blocks;
     		LMS_Ctrl_Packet_Tx->Header.Periph_ID = LMS_Ctrl_Packet_Rx->Header.Periph_ID;
     		LMS_Ctrl_Packet_Tx->Header.Status = STATUS_BUSY_CMD;

     		switch(LMS_Ctrl_Packet_Rx->Header.Command)
     		{
 				case CMD_GET_INFO:

 					LMS_Ctrl_Packet_Tx->Data_field[0] = FW_VER;
 					LMS_Ctrl_Packet_Tx->Data_field[1] = DEV_TYPE;
 					LMS_Ctrl_Packet_Tx->Data_field[2] = LMS_PROTOCOL_VER;
 					LMS_Ctrl_Packet_Tx->Data_field[3] = HW_VER;
 					LMS_Ctrl_Packet_Tx->Data_field[4] = EXP_BOARD;

 					LMS_Ctrl_Packet_Tx->Header.Status = STATUS_COMPLETED_CMD;
 				break;

 				case CMD_LMS_RST:

 					if(!Check_Periph_ID(MAX_ID_LMS7, LMS_Ctrl_Packet_Rx->Header.Periph_ID)) break;

 					switch (LMS_Ctrl_Packet_Rx->Data_field[0])
 					{
 						case LMS_RST_DEACTIVATE:

 		 					switch(LMS_Ctrl_Packet_Rx->Header.Periph_ID)
 		 					{
 		 						default:
 		 						case 0:
 		 							Modify_BRDSPI16_Reg_bits (BRD_SPI_REG_LMS1_LMS2_CTRL, LMS1_RESET, LMS1_RESET, 1); //high level
 		 						break;
 		 						case 1:
 		 							Modify_BRDSPI16_Reg_bits (BRD_SPI_REG_LMS1_LMS2_CTRL, LMS2_RESET, LMS2_RESET, 1); //high level
 		 						break;
 		 					}

 						break;

 						case LMS_RST_ACTIVATE:

 		 					switch(LMS_Ctrl_Packet_Rx->Header.Periph_ID)
 		 					{
 		 						default:
 		 						case 0:
 		 							Modify_BRDSPI16_Reg_bits (BRD_SPI_REG_LMS1_LMS2_CTRL, LMS1_RESET, LMS1_RESET, 0); //low level
 		 						break;
 		 						case 1:
 		 							Modify_BRDSPI16_Reg_bits (BRD_SPI_REG_LMS1_LMS2_CTRL, LMS2_RESET, LMS2_RESET, 0); //low level
 		 						break;
 		 					}

 						break;

 						case LMS_RST_PULSE:
 		 					switch(LMS_Ctrl_Packet_Rx->Header.Periph_ID)
 		 					{
 		 						default:
 		 						case 0:
 		 							Modify_BRDSPI16_Reg_bits (BRD_SPI_REG_LMS1_LMS2_CTRL, LMS1_RESET, LMS1_RESET, 0); //low level
 		 							Modify_BRDSPI16_Reg_bits (BRD_SPI_REG_LMS1_LMS2_CTRL, LMS1_RESET, LMS1_RESET, 1); //high level
 		 						break;
 		 						case 1:
 		 							Modify_BRDSPI16_Reg_bits (BRD_SPI_REG_LMS1_LMS2_CTRL, LMS2_RESET, LMS2_RESET, 0); //low level
 		 							Modify_BRDSPI16_Reg_bits (BRD_SPI_REG_LMS1_LMS2_CTRL, LMS2_RESET, LMS2_RESET, 1); //high level
 		 						break;
 		 					}

 						break;

 						default:
 							cmd_errors++;
 						break;
 					}

 					LMS_Ctrl_Packet_Tx->Header.Status = STATUS_COMPLETED_CMD;
 				break;

 				case CMD_LMS7002_WR:
 					if(!Check_Periph_ID(MAX_ID_LMS7, LMS_Ctrl_Packet_Rx->Header.Periph_ID)) break;
 					if(Check_many_blocks (4)) break;

 					for(block = 0; block < LMS_Ctrl_Packet_Rx->Header.Data_blocks; block++)
 					{
 						//Write LMS7 register
 						sbi(LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 4)], 7); //set write bit
 						if (LMS_Ctrl_Packet_Rx->Header.Periph_ID == 2) {
 							spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_LMS7002M_3_SS);
 						}
 						else if (LMS_Ctrl_Packet_Rx->Header.Periph_ID == 1) {
 							spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_LMS7002M_2_SS);
 						}
 						else
 						{
 							spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_LMS7002M_1_SS);
 						}
 						//spirez = alt_avalon_spi_command(FPGA_SPI0_BASE, LMS_Ctrl_Packet_Rx->Header.Periph_ID == 1 ? SPI_NR_LMS7002M_1 : SPI_NR_LMS7002M_0,
 								//4, &LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 4)], 0, NULL, 0);

 						spirez = XSpi_Transfer(&Spi0, &LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 4)], NULL, 4);
 					}

 					LMS_Ctrl_Packet_Tx->Header.Status = STATUS_COMPLETED_CMD;
 				break;

 				case CMD_LMS7002_RD:
 					if(Check_many_blocks (4)) break;

 					for(block = 0; block < LMS_Ctrl_Packet_Rx->Header.Data_blocks; block++)
 					{
 						//Read LMS7 register
 						cbi(LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 2)], 7);  //clear write bit
 						if (LMS_Ctrl_Packet_Rx->Header.Periph_ID == 2) {
 							spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_LMS7002M_3_SS);
 						}
 						else if (LMS_Ctrl_Packet_Rx->Header.Periph_ID == 1) {
 							spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_LMS7002M_2_SS);
 						}
 						else
 						{
 							spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_LMS7002M_1_SS);
 						}
 						//spirez = alt_avalon_spi_command(FPGA_SPI0_BASE, LMS_Ctrl_Packet_Rx->Header.Periph_ID == 1 ? SPI_NR_LMS7002M_1 : SPI_NR_LMS7002M_0,
 								//2, &LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 2)], 2, &LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)], 0);

 						spirez = XSpi_Transfer(&Spi0, &LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 2)], spi_ReadBuffer, 4);
 						LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)] = spi_ReadBuffer[2];
 						LMS_Ctrl_Packet_Tx->Data_field[3 + (block * 4)] = spi_ReadBuffer[3];

 					}

 					LMS_Ctrl_Packet_Tx->Header.Status = STATUS_COMPLETED_CMD;
 				break;

 	 			case CMD_BRDSPI16_WR:
 	 				if(Check_many_blocks (4)) break;

 	 				for(block = 0; block < LMS_Ctrl_Packet_Rx->Header.Data_blocks; block++)
 	 				{
 	 					//write reg addr
 	 					sbi(LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 4)], 7); //set write bit

 	 					//spirez = alt_avalon_spi_command(FPGA_SPI0_BASE, SPI_NR_FPGA, 4, &LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 4)], 0, NULL, 0);
 	 					spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);
 	 					spirez = XSpi_Transfer(&Spi0, &LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 4)], NULL, 4);
 	 				}

 	 				LMS_Ctrl_Packet_Tx->Header.Status = STATUS_COMPLETED_CMD;
 	 			break;

 				case CMD_BRDSPI16_RD:
 					if(Check_many_blocks (4)) break;

 					for(block = 0; block < LMS_Ctrl_Packet_Rx->Header.Data_blocks; block++)
 					{

 						//write reg addr
 						cbi(LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 2)], 7);  //clear write bit

 						//spirez = alt_avalon_spi_command(FPGA_SPI0_BASE, SPI_NR_FPGA, 2, &LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 2)], 2, &LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)], 0);
 						spirez = XSpi_SetSlaveSelect(&Spi0, SPI0_FPGA_SS);

 						spirez = XSpi_Transfer(&Spi0, &LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 2)], spi_ReadBuffer, 4);
 						LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)] = spi_ReadBuffer[2];
 						LMS_Ctrl_Packet_Tx->Data_field[3 + (block * 4)] = spi_ReadBuffer[3];

 					}

 					LMS_Ctrl_Packet_Tx->Header.Status = STATUS_COMPLETED_CMD;
 				break;

				case CMD_ANALOG_VAL_RD:

					for(block = 0; block < LMS_Ctrl_Packet_Rx->Header.Data_blocks; block++)
					{
						switch (LMS_Ctrl_Packet_Rx->Data_field[0 + (block)])//ch
						{
							case 0://dac val

								LMS_Ctrl_Packet_Tx->Data_field[0 + (block * 4)] = LMS_Ctrl_Packet_Rx->Data_field[block]; //ch
								LMS_Ctrl_Packet_Tx->Data_field[1 + (block * 4)] = 0x00; //RAW //unit, power

								//LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)] = 0; //signed val, MSB byte
								//LMS_Ctrl_Packet_Tx->Data_field[3 + (block * 4)] = dac_val; //signed val, LSB byte
								LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)] = (dac_val >> 8) & 0xFF; //unsigned val, MSB byte
								LMS_Ctrl_Packet_Tx->Data_field[3 + (block * 4)] = dac_val & 0xFF; //unsigned val, LSB byte

							break;

							case 1: //temperature

								//spirez = I2C_start(I2C_OPENCORES_0_BASE, LM75_I2C_ADDR, 0);
								//spirez = I2C_write(I2C_OPENCORES_0_BASE, 0x00, 1);				// Pointer = temperature register
								//spirez = I2C_start(I2C_OPENCORES_0_BASE, LM75_I2C_ADDR, 1);

							    //temperature
								ByteCount = 1;
								Iic_WriteBuffer[0] = 0x00;	// Pointer = TOS register
							    spirez = XIic_Send(XPAR_AXI_IIC_0_BASEADDR, LM75_I2C_ADDR, (u8 *)&Iic_WriteBuffer, ByteCount, XIIC_STOP);

								// Read temperature and recalculate
								//converted_val = (signed short int)I2C_read(I2C_OPENCORES_0_BASE, 0);
							    ByteCount = 2;
							    spirez = XIic_Recv(XPAR_AXI_IIC_0_BASEADDR, LM75_I2C_ADDR, (u8 *)&Iic_ReadBuffer, ByteCount, XIIC_STOP);

								converted_val = (signed short int) Iic_ReadBuffer[0];
								converted_val = converted_val << 8;
								converted_val = 10 * (converted_val >> 8);
								//spirez = I2C_read(I2C_OPENCORES_0_BASE, 1);
								//if(spirez & 0x80) converted_val = converted_val + 5;
								if(Iic_ReadBuffer[1] & 0x80) converted_val = converted_val + 5;


								LMS_Ctrl_Packet_Tx->Data_field[0 + (block * 4)] = LMS_Ctrl_Packet_Rx->Data_field[block]; //ch
								LMS_Ctrl_Packet_Tx->Data_field[1 + (block * 4)] = 0x50; //mC //unit, power

								LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)] = (converted_val >> 8); //signed val, MSB byte
								LMS_Ctrl_Packet_Tx->Data_field[3 + (block * 4)] = converted_val; //signed val, LSB byte

							break;
							/*
							case 2://wiper 0 position
								LMS_Ctrl_Packet_Tx->Data_field[0 + (block * 4)] = LMS_Ctrl_Packet_Rx->Data_field[block]; //ch
								LMS_Ctrl_Packet_Tx->Data_field[1 + (block * 4)] = 0x00; //RAW //unit, power

								LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)] = (wiper_pos[0] >> 8) & 0xFF; //signed val, MSB byte
								LMS_Ctrl_Packet_Tx->Data_field[3 + (block * 4)] = wiper_pos[0] & 0xFF; //signed val, LSB byte
							break;

							case 3://wiper 1 position
								LMS_Ctrl_Packet_Tx->Data_field[0 + (block * 4)] = LMS_Ctrl_Packet_Rx->Data_field[block]; //ch
								LMS_Ctrl_Packet_Tx->Data_field[1 + (block * 4)] = 0x00; //RAW //unit, power

								LMS_Ctrl_Packet_Tx->Data_field[2 + (block * 4)] = (wiper_pos[1] >> 8) & 0xFF; //signed val, MSB byte
								LMS_Ctrl_Packet_Tx->Data_field[3 + (block * 4)] = wiper_pos[1] & 0xFF; //signed val, LSB byte
							break;
							*/

							default:
								cmd_errors++;
							break;
						}
					}

					LMS_Ctrl_Packet_Tx->Header.Status = STATUS_COMPLETED_CMD;

				break;

				case CMD_ANALOG_VAL_WR:
					if(Check_many_blocks (4)) break;

					for(block = 0; block < LMS_Ctrl_Packet_Rx->Header.Data_blocks; block++)
					{
						switch (LMS_Ctrl_Packet_Rx->Data_field[0 + (block * 4)]) //do something according to channel
						{
							case 0: //TCXO DAC
								if (LMS_Ctrl_Packet_Rx->Data_field[1 + (block * 4)] == 0) //RAW units?
								{
									//Control_TCXO_ADF(0, NULL); //set ADF4002 CP to three-state

									//write data to DAC
									//dac_val = LMS_Ctrl_Packet_Rx->Data_field[3 + (block * 4)];
									dac_val = (LMS_Ctrl_Packet_Rx->Data_field[2 + (block * 4)] << 8 ) + LMS_Ctrl_Packet_Rx->Data_field[3 + (block * 4)];
									Control_TCXO_DAC(1, &dac_val); //enable DAC output, set new val
								}
								else cmd_errors++;

							break;

							default:
								cmd_errors++;
							break;
						}
					}

					if(cmd_errors) LMS_Ctrl_Packet_Tx->Header.Status = STATUS_ERROR_CMD;
					else LMS_Ctrl_Packet_Tx->Header.Status = STATUS_COMPLETED_CMD;

				break;

 				default:
 					/* This is unknown request. */
 					//isHandled = CyFalse;
 					LMS_Ctrl_Packet_Tx->Header.Status = STATUS_UNKNOWN_CMD;
 				break;
     		};

     		//Send response to the command
        	for(int i=0; i<64/sizeof(uint32_t); ++i)
        	{
        		AXI_TO_NATIVE_FIFO_mWriteReg(XPAR_AXI_TO_NATIVE_FIFO_0_S00_AXI_BASEADDR, AXI_TO_NATIVE_FIFO_S00_AXI_SLV_REG0_OFFSET, dest[i]);
			}


        }

    }





    //print("Hello World\n\r");

    cleanup_platform();
    return 0;
}