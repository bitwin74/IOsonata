/**-------------------------------------------------------------------------
@file	qspi_stm32l4xx.cpp

@brief	QUAD SPI implementation on STM32L4xx series MCU

@author	Hoang Nguyen Hoan
@date	July 26, 2019

@license

Copyright (c) 2019, I-SYST inc., all rights reserved

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies, and none of the
names : I-SYST or its contributors may be used to endorse or
promote products derived from this software without specific prior written
permission.

For info or contributing contact : hnhoan at i-syst dot com

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------------*/
#include "stm32l4xx.h"

#include "istddef.h"
#include "coredev/spi.h"
#include "iopinctrl.h"

#define STM32L4XX_QSPI_MAXDEV		1

#pragma pack(push, 4)
typedef struct {
	int DevNo;
	SPIDEV *pSpiDev;
	QUADSPI_TypeDef	*pReg;	// Master I/O register map
} STM32L4XX_QSPIDEV;
#pragma pack(pop)

static STM32L4XX_QSPIDEV s_STM32L4xxQSPIDev = {
	0, NULL, QUADSPI
};

static bool STM32L4xxQSPIWaitReady(STM32L4XX_QSPIDEV * const pDev, uint32_t Timeout)
{
	do {
        if (pDev->pReg->EVENTS_READY)
        {
            pDev->pReg->EVENTS_READY = 0; // clear event
            return true;
        }
    } while (Timeout-- > 0);

    return false;
}

static int STM32L4xxQSPIGetRate(DEVINTRF * const pDev)
{
	int rate = 0;

	if (pDev && pDev->pDevData)
		rate = ((NRF52_SPIDEV*)pDev->pDevData)->pSpiDev->Cfg.Rate;

	return rate;
}

// Set data rate in bits/sec (Hz)
// return actual rate
static int STM32L4xxQSPISetRate(DEVINTRF * const pDev, int DataRate)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev->pDevData;

	if (DataRate < 250000)
	{
		dev->pReg->FREQUENCY = SPI_FREQUENCY_FREQUENCY_K125;
		dev->pSpiDev->Cfg.Rate = 125000;
	}
	else if (DataRate < 500000)
	{
		dev->pReg->FREQUENCY = SPI_FREQUENCY_FREQUENCY_K250;
		dev->pSpiDev->Cfg.Rate = 250000;
	}
	else if (DataRate < 1000000)
	{
		dev->pReg->FREQUENCY = SPI_FREQUENCY_FREQUENCY_K500;
		dev->pSpiDev->Cfg.Rate = 500000;
	}
	else if (DataRate < 2000000)
	{
		dev->pReg->FREQUENCY = SPI_FREQUENCY_FREQUENCY_M1;
		dev->pSpiDev->Cfg.Rate = 1000000;
	}
	else if (DataRate < 4000000)
	{
		dev->pReg->FREQUENCY = SPI_FREQUENCY_FREQUENCY_M2;
		dev->pSpiDev->Cfg.Rate = 2000000;
	}
	else if (DataRate < 8000000)
	{
		dev->pReg->FREQUENCY = SPI_FREQUENCY_FREQUENCY_M4;
		dev->pSpiDev->Cfg.Rate = 4000000;
	}
#ifdef NRF52840_XXAA
	else if (DataRate < 16000000)
	{
		dev->pReg->FREQUENCY = SPI_FREQUENCY_FREQUENCY_M8;
		dev->pSpiDev->Cfg.Rate = 8000000;
	}
	else if (DataRate < 32000000)
	{
		dev->pReg->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M16;
		dev->pSpiDev->Cfg.Rate = 16000000;
	}
	else
	{
		dev->pReg->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M32;
		dev->pSpiDev->Cfg.Rate = 32000000;
	}
#else
	else
	{
		dev->pReg->FREQUENCY = SPI_FREQUENCY_FREQUENCY_M8;
		dev->pSpiDev->Cfg.Rate = 8000000;
	}
#endif
	return dev->pSpiDev->Cfg.Rate;
}

void STM32L4xxQSPIDisable(DEVINTRF * const pDev)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev->pDevData;

	if (dev->pSpiDev->Cfg.Mode == SPIMODE_SLAVE)
	{
		dev->pDmaSReg->ENABLE = (SPIS_ENABLE_ENABLE_Disabled << SPIS_ENABLE_ENABLE_Pos);
	}
#ifdef NRF52_SERIES
	else if (pDev->bDma)
	{
		dev->pDmaReg->ENABLE = (SPIM_ENABLE_ENABLE_Disabled << SPIM_ENABLE_ENABLE_Pos);
	}
#endif
	else
	{
        dev->pReg->ENABLE = (SPI_ENABLE_ENABLE_Disabled << SPI_ENABLE_ENABLE_Pos);
	}
}

static void STM32L4xxQSPIEnable(DEVINTRF * const pDev)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev->pDevData;

	if (dev->pSpiDev->Cfg.Mode == SPIMODE_SLAVE)
	{
		dev->pDmaSReg->ENABLE = (SPIS_ENABLE_ENABLE_Enabled << SPIS_ENABLE_ENABLE_Pos);
	}
#ifdef NRF52_SERIES
	else if (pDev->bDma)
	{
		dev->pDmaReg->ENABLE = (SPIM_ENABLE_ENABLE_Enabled << SPIM_ENABLE_ENABLE_Pos);
	}
#endif
	else
	{
        dev->pReg->ENABLE = (SPI_ENABLE_ENABLE_Enabled << SPI_ENABLE_ENABLE_Pos);
	}
}

static void STM32L4xxQSPIPowerOff(DEVINTRF * const pDev)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev->pDevData;

	// Undocumented Power down.  Nordic Bug with DMA causing high current consumption
	*(volatile uint32_t *)((uint32_t)dev->pReg + 0xFFC);
	*(volatile uint32_t *)((uint32_t)dev->pReg + 0xFFC) = 1;
	*(volatile uint32_t *)((uint32_t)dev->pReg + 0xFFC) = 0;
}

// Initial receive
static bool STM32L4xxQSPIStartRx(DEVINTRF * const pDev, int DevCs)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev->pDevData;

	if (dev->pSpiDev->Cfg.ChipSel == SPICSEL_MAN)
		return true;

	if (DevCs < 0 || DevCs >= dev->pSpiDev->Cfg.NbIOPins - SPI_SS_IOPIN_IDX)
		return false;

	dev->pSpiDev->CurDevCs = DevCs;
	IOPinClear(dev->pSpiDev->Cfg.pIOPinMap[DevCs + SPI_SS_IOPIN_IDX].PortNo,
			   dev->pSpiDev->Cfg.pIOPinMap[DevCs + SPI_SS_IOPIN_IDX].PinNo);

	if (dev->pSpiDev->Cfg.Type == SPITYPE_3WIRE)
	{
#ifdef NRF52_SERIES
        dev->pReg->PSEL.MISO = (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
		dev->pReg->PSEL.MOSI = -1;
#else
        dev->pReg->PSELMISO = (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
		dev->pReg->PSELMOSI = -1;
#endif
	}

	return true;
}

// Receive Data only, no Start/Stop condition
static int STM32L4xxQSPIRxDataDma(DEVINTRF * const pDev, uint8_t *pBuff, int BuffLen)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev-> pDevData;
	int cnt = 0;

#ifdef NRF52_SERIES
    dev->pDmaReg->TXD.PTR = 0;
    dev->pDmaReg->TXD.MAXCNT = 0;
    dev->pDmaReg->TXD.LIST = 0;
    dev->pDmaReg->RXD.PTR = (uint32_t)pBuff;
    dev->pDmaReg->RXD.LIST = SPIM_RXD_LIST_LIST_ArrayList << SPIM_RXD_LIST_LIST_Pos;

    while (BuffLen > 0)
	{
		int l = min(BuffLen, NRF5X_SPI_DMA_MAXCNT);

		dev->pDmaReg->RXD.MAXCNT = l;
		dev->pDmaReg->EVENTS_END = 0;
		dev->pDmaReg->EVENTS_ENDRX = 0;
		dev->pDmaReg->TASKS_START = 1;

		if (nRF52SPIWaitRX(dev, 100000) == false)
			break;

        l = dev->pDmaReg->RXD.AMOUNT;
		BuffLen -= l;
		pBuff += l;
		cnt += l;
	}
#endif

	return cnt;
}

// Receive Data only, no Start/Stop condition
static int STM32L4xxQSPIRxData(DEVINTRF * const pDev, uint8_t *pBuff, int BuffLen)
{
    NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev-> pDevData;
    int cnt = 0;

    dev->pReg->EVENTS_READY = 0;

    while (BuffLen > 0)
    {
        dev->pReg->TXD = 0xFF;

        if (nRF5xSPIWaitReady(dev, 100000) == false)
            break;

        *pBuff = dev->pReg->RXD;

        BuffLen--;
        pBuff++;
        cnt++;
    }

    return cnt;
}

// Stop receive
static void STM32L4xxQSPIStopRx(DEVINTRF * const pDev)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev-> pDevData;

	if (dev->pSpiDev->Cfg.ChipSel == SPICSEL_AUTO)
	{
		IOPinSet(dev->pSpiDev->Cfg.pIOPinMap[dev->pSpiDev->CurDevCs + SPI_SS_IOPIN_IDX].PortNo,
				dev->pSpiDev->Cfg.pIOPinMap[dev->pSpiDev->CurDevCs + SPI_SS_IOPIN_IDX].PinNo);
	}

	if (dev->pSpiDev->Cfg.Type == SPITYPE_3WIRE)
	{
#ifdef NRF52_SERIES
//		dev->pReg->PSEL.MOSI = (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
#else
//		dev->pReg->PSELMOSI = (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
#endif
	}
}

// Initiate transmit
static bool STM32L4xxQSPIStartTx(DEVINTRF * const pDev, int DevCs)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev-> pDevData;

	if (dev->pSpiDev->Cfg.ChipSel == SPICSEL_MAN)
		return true;

	if (DevCs < 0 || DevCs >= dev->pSpiDev->Cfg.NbIOPins - SPI_SS_IOPIN_IDX)
		return false;

	dev->pSpiDev->CurDevCs = DevCs;
	IOPinClear(dev->pSpiDev->Cfg.pIOPinMap[DevCs + SPI_SS_IOPIN_IDX].PortNo,
			   dev->pSpiDev->Cfg.pIOPinMap[DevCs + SPI_SS_IOPIN_IDX].PinNo);

    if (dev->pSpiDev->Cfg.Type == SPITYPE_3WIRE)
    {
#ifdef NRF52_SERIES
        dev->pReg->PSEL.MOSI = (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
        dev->pReg->PSEL.MISO = -1;
#else
        dev->pReg->PSELMOSI = (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
        dev->pReg->PSELMISO = -1;
#endif
    }
	return true;
}

// Transmit Data only, no Start/Stop condition
static int STM32L4xxQSPITxDataDma(DEVINTRF * const pDev, uint8_t *pData, int DataLen)
{
	NRF52_SPIDEV *dev = (NRF52_SPIDEV *)pDev-> pDevData;
	int cnt = 0;

#ifdef NRF52_SERIES
	dev->pDmaReg->RXD.PTR = 0;
    dev->pDmaReg->RXD.MAXCNT = 0;
    dev->pDmaReg->RXD.LIST = 0;
    dev->pDmaReg->TXD.PTR = (uint32_t)pData;
    dev->pDmaReg->TXD.LIST = SPIM_TXD_LIST_LIST_ArrayList << SPIM_TXD_LIST_LIST_Pos;

    while (DataLen > 0)
	{
		int l = min(DataLen, NRF5X_SPI_DMA_MAXCNT);
		dev->pDmaReg->TXD.MAXCNT = l;
		dev->pDmaReg->EVENTS_END = 0;
		dev->pDmaReg->EVENTS_ENDTX = 0;
		dev->pDmaReg->TASKS_START = 1;

		if (nRF52SPIWaitDMA(dev, 100000) == false)
		{
		    break;
		}

		l = dev->pDmaReg->TXD.AMOUNT;
		DataLen -= l;
		pData += l;
		cnt += l;
	}
#endif
	return cnt;
}

// Send Data only, no Start/Stop condition
static int STM32L4xxQSPITxData(DEVINTRF *pDev, uint8_t *pData, int DataLen)
{
    NRF52_SPIDEV *dev = (NRF52_SPIDEV*)pDev->pDevData;
    int cnt = 0;

    if (pData == NULL)
    {
        return 0;
    }

    while (DataLen > 0)
    {
        dev->pReg->TXD = *pData;

        if (nRF5xSPIWaitReady(dev, 10000) == false)
        {
            break;
        }

        int d = dev->pReg->RXD;

        DataLen--;
        pData++;
        cnt++;
    }

    return cnt;
}

// Stop transmit
static void STM32L4xxQSPIStopTx(DEVINTRF * const pDev)
{
	STM32L4XX_QSPIDEV *dev = (STM32L4XX_QSPIDEV *)pDev-> pDevData;

	if (dev->pSpiDev->Cfg.ChipSel == SPICSEL_AUTO)
	{
		IOPinSet(dev->pSpiDev->Cfg.pIOPinMap[dev->pSpiDev->CurDevCs + SPI_SS_IOPIN_IDX].PortNo,
				dev->pSpiDev->Cfg.pIOPinMap[dev->pSpiDev->CurDevCs + SPI_SS_IOPIN_IDX].PinNo);
	}
    if (dev->pSpiDev->Cfg.Type == SPITYPE_3WIRE)
    {
#ifdef NRF52_SERIES
        dev->pReg->PSEL.MOSI = -1;
        dev->pReg->PSEL.MISO = (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
#else
        dev->pReg->PSELMOSI = -1;
        dev->pReg->PSELMISO = (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (dev->pSpiDev->Cfg.pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
#endif
    }
}

void SPIIrqHandler(int DevNo)
{
	STM32L4XX_QSPIDEV *dev = s_STM32L4xxQSPIDev;

	if (dev->pSpiDev->Cfg.Mode == SPIMODE_SLAVE)
	{
		if (dev->pDmaSReg->EVENTS_ENDRX)
		{
			if (dev->pSpiDev->Cfg.EvtCB)
			{
				dev->pSpiDev->Cfg.EvtCB(pDev, DEVINTRF_EVT_RX_FIFO_FULL, NULL, 0);
			}
			dev->pDmaSReg->EVENTS_ENDRX = 0;
			dev->pDmaSReg->STATUS = dev->pDmaSReg->STATUS;
		}

		if (dev->pDmaSReg->EVENTS_END)
		{
			if (dev->pSpiDev->Cfg.EvtCB)
			{
#ifdef NRF52_SERIES
				dev->pSpiDev->Cfg.EvtCB(pDev, DEVINTRF_EVT_COMPLETED, (uint8_t*)dev->pDmaSReg->RXD.PTR, dev->pDmaSReg->RXD.AMOUNT);
#else
				dev->pSpiDev->Cfg.EvtCB(pDev, DEVINTRF_EVT_COMPLETED, (uint8_t*)dev->pDmaSReg->RXDPTR, dev->pDmaSReg->AMOUNTRX);
#endif
			}
			dev->pDmaSReg->EVENTS_END = 0;
		}

		if (dev->pDmaSReg->EVENTS_ACQUIRED)
		{
			if (dev->pSpiDev->Cfg.EvtCB)
			{
				dev->pSpiDev->Cfg.EvtCB(pDev, DEVINTRF_EVT_STATECHG, NULL, 0);
			}
			dev->pDmaSReg->STATUS = dev->pDmaSReg->STATUS;

#ifdef NRF52_SERIES
			dev->pDmaSReg->RXD.PTR = (uint32_t)dev->pSpiDev->pRxBuff[0];
			dev->pDmaSReg->RXD.MAXCNT = dev->pSpiDev->RxBuffLen[0];
			dev->pDmaSReg->TXD.PTR = (uint32_t)dev->pSpiDev->pTxData[0];
			dev->pDmaSReg->TXD.MAXCNT = dev->pSpiDev->TxDataLen[0];
#else
			dev->pDmaSReg->RXDPTR = (uint32_t)dev->pSpiDev->pRxBuff[0];
			dev->pDmaSReg->MAXRX = dev->pSpiDev->RxBuffLen[0];
			dev->pDmaSReg->TXDPTR = (uint32_t)dev->pSpiDev->pTxData[0];
			dev->pDmaSReg->MAXTX = dev->pSpiDev->TxDataLen[0];
#endif
			dev->pDmaSReg->EVENTS_ACQUIRED = 0;
			dev->pDmaSReg->TASKS_RELEASE = 1;
		}
	}
}

bool SPIInit(SPIDEV * const pDev, const SPICFG *pCfgData)
{
	QUADSPI_Typedef *reg;
	uint32_t cfgreg = 0;

	if (pDev == NULL || pCfgData == NULL)
	{
		return false;
	}

	if (pCfgData->DevNo < 0 || pCfgData->DevNo >= STM32L4XX_QSPI_MAXDEV || pCfgData->NbIOPins < 6)
	{
		return false;
	}

	// Get the correct register map
	reg = s_STM32L4xxQSPIDev.pReg;

	// Configure I/O pins
	IOPinCfg(pCfgData->pIOPinMap, pCfgData->NbIOPins);

	for (int i = SPI_SS_IOPIN_IDX; i < pCfgData->NbIOPins; i++)
	{
		IOPinSet(pCfgData->pIOPinMap[i].PortNo, pCfgData->pIOPinMap[i].PinNo);
	}

	if (pCfgData->BitOrder == SPIDATABIT_LSB)
	{
		cfgreg |= SPI_CONFIG_ORDER_LsbFirst;
	}
	else
	{
		cfgreg |= SPI_CONFIG_ORDER_MsbFirst;
	}

	if (pCfgData->DataPhase == SPIDATAPHASE_SECOND_CLK)
	{
		cfgreg |= (SPI_CONFIG_CPHA_Trailing   << SPI_CONFIG_CPHA_Pos);
	}
	else
	{
		cfgreg |= (SPI_CONFIG_CPHA_Leading    << SPI_CONFIG_CPHA_Pos);
	}

	if (pCfgData->ClkPol == SPICLKPOL_LOW)
	{
		cfgreg |= (SPI_CONFIG_CPOL_ActiveLow  << SPI_CONFIG_CPOL_Pos);
		IOPinSet(0, pCfgData->pIOPinMap[SPI_SCK_IOPIN_IDX].PinNo);
	}
	else
	{
		cfgreg |= (SPI_CONFIG_CPOL_ActiveHigh << SPI_CONFIG_CPOL_Pos);
		IOPinClear(0, pCfgData->pIOPinMap[SPI_SCK_IOPIN_IDX].PinNo);
	}

	reg->CONFIG = cfgreg;

	pDev->Cfg = *pCfgData;
	s_STM32L4xxSPIDev[pCfgData->DevNo].pSpiDev  = pDev;
	pDev->DevIntrf.pDevData = (void*)&s_STM32L4xxSPIDev[pCfgData->DevNo];

	STM32L4xxSPISetRate(&pDev->DevIntrf, pCfgData->Rate);

	pDev->DevIntrf.Type = DEVINTRF_TYPE_SPI;
	pDev->DevIntrf.Disable = STM32L4xxQSPIDisable;
	pDev->DevIntrf.Enable = STM32L4xxQSPIEnable;
	pDev->DevIntrf.GetRate = STM32L4xxQSPIGetRate;
	pDev->DevIntrf.SetRate = STM32L4xxQSPISetRate;
	pDev->DevIntrf.StartRx = STM32L4xxQSPIStartRx;
	pDev->DevIntrf.RxData = STM32L4xxQSPIRxData;
	pDev->DevIntrf.StopRx = STM32L4xxQSPIStopRx;
	pDev->DevIntrf.StartTx = STM32L4xxQSPIStartTx;
	pDev->DevIntrf.TxData = STM32L4xxQSPITxData;
	pDev->DevIntrf.StopTx = STM32L4xxQSPIStopTx;
	pDev->DevIntrf.IntPrio = pCfgData->IntPrio;
	pDev->DevIntrf.EvtCB = pCfgData->EvtCB;
	pDev->DevIntrf.EnCnt = 1;
	pDev->DevIntrf.MaxRetry = pCfgData->MaxRetry;
	pDev->DevIntrf.bDma = pCfgData->bDmaEn;
	pDev->DevIntrf.PowerOff = STM32L4xxQSPIPowerOff;
	atomic_flag_clear(&pDev->DevIntrf.bBusy);

	if (pCfgData->Mode == SPIMODE_SLAVE)
	{
		// Force DMA for slave mode
		pDev->DevIntrf.bDma = true;
	}

	if (pDev->DevIntrf.bDma == true)
	{
		pDev->DevIntrf.RxData = nRF52SPIRxDataDma;
		pDev->DevIntrf.TxData = nRF52SPITxDataDma;
	}

	uint32_t inten = 0;

    if (pCfgData->Mode == SPIMODE_SLAVE)
	{
		NRF_SPIS_Type *sreg = s_nRF52SPIDev[pCfgData->DevNo].pDmaSReg;

#ifdef NRF52_SERIES
        sreg->PSEL.SCK = (pCfgData->pIOPinMap[SPI_SCK_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_SCK_IOPIN_IDX].PortNo << 5);
        sreg->PSEL.MISO = (pCfgData->pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
        sreg->PSEL.MOSI = (pCfgData->pIOPinMap[SPI_MOSI_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_MOSI_IOPIN_IDX].PortNo << 5);
		sreg->PSEL.CSN = (pCfgData->pIOPinMap[SPI_SS_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_SS_IOPIN_IDX].PortNo << 5);
#else
        sreg->PSELSCK = (pCfgData->pIOPinMap[SPI_SCK_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_SCK_IOPIN_IDX].PortNo << 5);
        sreg->PSELMISO = (pCfgData->pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
        sreg->PSELMOSI = (pCfgData->pIOPinMap[SPI_MOSI_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_MOSI_IOPIN_IDX].PortNo << 5);
		sreg->PSELCSN = (pCfgData->pIOPinMap[SPI_SS_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_SS_IOPIN_IDX].PortNo << 5);
#endif
		sreg->ORC = 0xFF;
		sreg->STATUS = sreg->STATUS;
		sreg->EVENTS_ENDRX = 0;
		sreg->EVENTS_END = 0;
		sreg->EVENTS_ACQUIRED = 0;
		sreg->DEF = 0xFF;
		sreg->SHORTS = (SPIS_SHORTS_END_ACQUIRE_Enabled << SPIS_SHORTS_END_ACQUIRE_Pos);

		inten = (SPIS_INTENSET_ACQUIRED_Enabled << SPIS_INTENSET_ACQUIRED_Pos) |
				(SPIS_INTENSET_ENDRX_Enabled << SPIS_INTENSET_ENDRX_Pos) |
				(SPIS_INTENSET_END_Enabled << SPIS_INTENSET_END_Pos);
		reg->ENABLE =  (SPIS_ENABLE_ENABLE_Enabled << SPIS_ENABLE_ENABLE_Pos);
		sreg->TASKS_ACQUIRE = 1;	// Active event to update rx/tx buffer
	}
	else
	{
		reg->PSELSCK = (pCfgData->pIOPinMap[SPI_SCK_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_SCK_IOPIN_IDX].PortNo << 5);
		reg->PSELMISO = (pCfgData->pIOPinMap[SPI_MISO_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_MISO_IOPIN_IDX].PortNo << 5);
		reg->PSELMOSI = (pCfgData->pIOPinMap[SPI_MOSI_IOPIN_IDX].PinNo & 0x1f) | (pCfgData->pIOPinMap[SPI_MOSI_IOPIN_IDX].PortNo << 5);

#ifdef NRF52_SERIES
		if (pDev->DevIntrf.bDma == true)
		{
			s_nRF52SPIDev[pCfgData->DevNo].pDmaReg->ORC = 0xFF;
			s_nRF52SPIDev[pCfgData->DevNo].pDmaReg->ENABLE = (SPIM_ENABLE_ENABLE_Enabled << SPIM_ENABLE_ENABLE_Pos);
		}
		else
#endif
		{
			reg->ENABLE = (SPI_ENABLE_ENABLE_Enabled << SPI_ENABLE_ENABLE_Pos);
		}

        s_nRF52SPIDev[pCfgData->DevNo].pReg->EVENTS_READY = 0;
	}

    if (pCfgData->bIntEn)
    {
    	SetI2cSpiIntHandler(pCfgData->DevNo, &pDev->DevIntrf, SPIIrqHandler);

    	switch (pCfgData->DevNo)
    	{
    		case 0:
                NVIC_ClearPendingIRQ(SPI1_IRQn);
                NVIC_SetPriority(SPI1_IRQn, pCfgData->IntPrio);
                NVIC_EnableIRQ(SPI1_IRQn);
                break;
    	    case 1:
                NVIC_ClearPendingIRQ(SPI2_IRQn);
                NVIC_SetPriority(SPI2_IRQn, pCfgData->IntPrio);
                NVIC_EnableIRQ(SPI2_IRQn);
                break;
    	    case 2:
                NVIC_ClearPendingIRQ(SPI3_IRQn);
                NVIC_SetPriority(SPI3_IRQn, pCfgData->IntPrio);
                NVIC_EnableIRQ(SPI3_IRQn);
                break;
    	}

    	reg->INTENSET = inten;
    }

	return true;
}

extern "C" void QUADSPI_IRQHandler(void)
{
	QSPIIrqHandler(0);
    NVIC_ClearPendingIRQ(SPI1_IRQn);
}

