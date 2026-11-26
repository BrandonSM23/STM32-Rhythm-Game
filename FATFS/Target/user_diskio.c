/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
 /* USER CODE END Header */

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include "ff_gen_drv.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#ifdef SD_CS_LOW
#undef SD_CS_LOW
#endif
#define SD_CS_LOW() HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET)

#ifdef SD_CS_HIGH
#undef SD_CS_HIGH
#endif
#define SD_CS_HIGH() HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET)

#define CMD0    (0x40 | 0)   /* GO_IDLE_STATE */
#define CMD8    (0x40 | 8)   /* SEND_IF_COND */
#define CMD17   (0x40 | 17)  /* READ_SINGLE_BLOCK */
#define CMD24   (0x40 | 24)  /* WRITE_BLOCK */
#define CMD55   (0x40 | 55)  /* APP_CMD */
#define CMD58   (0x40 | 58)  /* READ_OCR */
#define ACMD41  (0x40 | 41)  /* APP_SEND_OP_COND */
/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
static uint8_t CardType = 0;

static uint8_t SPI_TxRx(uint8_t data){
	uint8_t rx_data = 0xFF;
	HAL_SPI_TransmitReceive(&hspi2, &data, &rx_data, 1, 100);
	return rx_data;
}

static uint8_t SD_SendCmd(uint8_t cmd, uint32_t arg){

  uint8_t crc = 0x00;
  if (cmd == CMD0) crc = 0x95;  /* Valid CRC for CMD0(0) */
  if (cmd == CMD8) crc = 0x87;  /* Valid CRC for CMD8(0x1AA) */

  SD_CS_HIGH();
  SPI_TxRx(0xFF);
  SD_CS_LOW();
  SPI_TxRx(0xFF);

  /* Send 6-byte Command Frame */
  SPI_TxRx(cmd);
  SPI_TxRx((uint8_t)(arg >> 24));
  SPI_TxRx((uint8_t)(arg >> 16));
  SPI_TxRx((uint8_t)(arg >> 8));
  SPI_TxRx((uint8_t)arg);
  SPI_TxRx(crc);

  /* Wait for response (up to 10 bytes) */
  uint8_t res = 0xFF;
  for (int i = 0; i < 10; i++)
  {
    res = SPI_TxRx(0xFF);
    if ((res & 0x80) == 0) break;
  }
  return res;
}

static void SD_SetFastClock(void)
{
  // 1. Disable SPI2 before reconfiguring
  __HAL_SPI_DISABLE(&hspi2);

  // 2. Clear Baud Rate bits (Bits 5:3 in CR1)
  hspi2.Instance->CR1 &= ~SPI_CR1_BR;

  // 3. Set new Baud Rate Prescaler: Prescaler 4 (BR[2:0] = 001)
  // Options:
  // SPI_BAUDRATEPRESCALER_4  -> ~10.5 MHz (Recommended for breadboards)
  // SPI_BAUDRATEPRESCALER_2  -> ~21.0 MHz (Requires very short wires)
  hspi2.Instance->CR1 |= SPI_BAUDRATEPRESCALER_4;

  // 4. Re-enable SPI2
  __HAL_SPI_ENABLE(&hspi2);
}

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
	if (pdrv != 0) return STA_NOINIT;

	uint8_t res;
	uint8_t ocr[4] = {0};

	printf("[SD INIT] Sending >= 74 dummy clocks...\r\n");
	SD_CS_HIGH();
	for (int i = 0; i < 10; i++)
	{
		SPI_TxRx(0xFF);
	}

	printf("[SD INIT] Sending CMD0 (Reset to SPI)...\r\n");
	res = SD_SendCmd(CMD0, 0);
	SD_CS_HIGH();
	SPI_TxRx(0xFF);

	printf("[SD INIT] CMD0 returned: 0x%02X (Expected <= 0x01)\r\n", res);

	if (res > 0x01)
	{
		Stat = STA_NOINIT;
		return Stat;
	}

	printf("[SD INIT] Sending CMD8 (Check condition)...\r\n");
	// Keep CS LOW to capture R7 response (1 byte status + 4 bytes data)

	SD_CS_HIGH();
	SPI_TxRx(0xFF);
	SD_CS_LOW();
	SPI_TxRx(0xFF);

	  // Send CMD8 frame manually or adapt SD_SendCmd
	SPI_TxRx(CMD8);
	SPI_TxRx(0x00);
	SPI_TxRx(0x00);
	SPI_TxRx(0x01);
	SPI_TxRx(0xAA);
	SPI_TxRx(0x87); // Valid CRC for CMD8(0x1AA)

	  // Wait for R1 status byte
	for (int i = 0; i < 10; i++)
	{
		res = SPI_TxRx(0xFF);
		if ((res & 0x80) == 0) break;
	}
	printf("[SD INIT] CMD8 returned: 0x%02X\r\n", res);

	if (res <= 0x01) // Valid response: 0x01 (Idle) or 0x00 (Ready)
	{
		// Read 4-byte payload of R7
		for (int i = 0; i < 4; i++) ocr[i] = SPI_TxRx(0xFF);
		SD_CS_HIGH();
		SPI_TxRx(0xFF);

		printf("[SD INIT] OCR: %02X %02X %02X %02X\r\n", ocr[0], ocr[1], ocr[2], ocr[3]);

	    // Send ACMD41 loop
		printf("[SD INIT] Sending ACMD41 loop...\r\n");
		uint32_t timeout = HAL_GetTick() + 1500;
		do {
			SD_SendCmd(CMD55, 0);
			SD_CS_HIGH();
			SPI_TxRx(0xFF);

			res = SD_SendCmd(ACMD41, 0x40000000); // HCS = 1
			SD_CS_HIGH();
			SPI_TxRx(0xFF);
		} while (res != 0x00 && HAL_GetTick() < timeout);

		printf("[SD INIT] ACMD41 returned: 0x%02X (Expected: 0x00)\r\n", res);

		if (res == 0x00)
		{
			SD_CS_HIGH();
			SPI_TxRx(0xFF);
			SD_CS_LOW();
			SPI_TxRx(0xFF);

			SPI_TxRx(CMD58);
			SPI_TxRx(0x00);
			SPI_TxRx(0x00);
			SPI_TxRx(0x00);
			SPI_TxRx(0x00);
			SPI_TxRx(0x01); // Dummy CRC

			// Wait for R1 status
			for (int i = 0; i < 10; i++)
			{
				res = SPI_TxRx(0xFF);
				if ((res & 0x80) == 0) break;
			}

			if (res == 0x00)
			{
				for (int i = 0; i < 4; i++) ocr[i] = SPI_TxRx(0xFF);
				CardType = (ocr[0] & 0x40) ? 4 : 2;
				printf("[SD INIT] OCR[0]=0x%02X -> Card type: %s (Type %d)\r\n",
						ocr[0], (CardType == 4) ? "SDHC/SDXC" : "SDSC", CardType);
			}
			SD_CS_HIGH();
			SPI_TxRx(0xFF);

			Stat &= ~STA_NOINIT; // Initialization SUCCESS
			printf("[SD INIT] Disk status cleared: STA_READY!\r\n");

			// Switch to high-speed SPI for data transfer phase
			SD_SetFastClock();
			printf("[SD INIT] SPI2 Clock switched to High Speed (~10.5 MHz)!\r\n");
		}
		else
		{
			printf("[SD INIT ERROR] ACMD41 timed out. Last res: 0x%02X\r\n", res);
		}
	}
	else
	{
		SD_CS_HIGH();
		SPI_TxRx(0xFF);
		printf("[SD INIT] CMD8 rejected (Legacy card or error). Setting STA_NOINIT.\r\n");
	}

	return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
    if (pdrv != 0) return STA_NOINIT;
    return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */

	  if (pdrv != 0 || (Stat & STA_NOINIT)) return RES_NOTRDY;

	  /* If standard SDv2 (byte-addressed), multiply LBA by 512 */
	  if (CardType != 4) sector *= 512;

	  while (count--)
	  {
		  SD_CS_HIGH();
		  SPI_TxRx(0xFF);
		  SD_CS_LOW();
		  SPI_TxRx(0xFF);

		  SPI_TxRx(CMD17);
		  SPI_TxRx((uint8_t)(sector >> 24));
		  SPI_TxRx((uint8_t)(sector >> 16));
		  SPI_TxRx((uint8_t)(sector >> 8));
		  SPI_TxRx((uint8_t)sector);
		  SPI_TxRx(0x01); // Dummy CRC

		      // Wait for command response (R1)
		  uint8_t res = 0xFF;
		  for (int i = 0; i < 10; i++)
		  {
			  res = SPI_TxRx(0xFF);
			  if ((res & 0x80) == 0) break;
		  }

		  if (res != 0x00)
		  {
			  printf("[SD READ ERROR] CMD17 failed with 0x%02X at sector %lu\r\n", res, sector);
			  SD_CS_HIGH();
			  SPI_TxRx(0xFF);
		      return RES_ERROR;
		  }

		      // Wait for Data Token (0xFE)
		  uint32_t timeout = HAL_GetTick() + 250;
		  while (SPI_TxRx(0xFF) != 0xFE)
		  {
			  if (HAL_GetTick() > timeout)
			  {
				  printf("[SD READ ERROR] Timed out waiting for 0xFE data token\r\n");
				  SD_CS_HIGH();
		          SPI_TxRx(0xFF);
		          return RES_ERROR;
			  }
		  }

		      // Clock in the 512-byte payload
		  HAL_SPI_Receive(&hspi2, buff, 512, 1000);

		      // Discard 2-byte CRC
		  SPI_TxRx(0xFF);
		  SPI_TxRx(0xFF);

		      // Deassert CS and send 8 dummy clocks
		  SD_CS_HIGH();
		  SPI_TxRx(0xFF);

		  buff += 512;
		  if (CardType == 4) sector += 1;
		  else sector += 512;
	  }

	  return RES_OK;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
	if (pdrv != 0 || (Stat & STA_NOINIT)) return RES_NOTRDY;

	  // Convert LBA to byte address for standard SDv2 cards
	if (CardType != 4) sector *= 512;

	while (count--)
	{    // 1. Send CMD24 (WRITE_BLOCK)
		if (SD_SendCmd(CMD24, sector) != 0x00)
		{
	      SD_CS_HIGH();
	      SPI_TxRx(0xFF);
	      return RES_ERROR;
	    }

	    // 2. Send Start Block Token (0xFE)
	    SPI_TxRx(0xFF); // Brief pause
	    SPI_TxRx(0xFE); // 0xFE = Data token for single block write

	    // 3. Transmit the 512-byte block
	    HAL_SPI_Transmit(&hspi2, (uint8_t *)buff, 512, 1000);

	    // 4. Send 2 dummy CRC bytes
	    SPI_TxRx(0xFF);
	    SPI_TxRx(0xFF);

	    // 5. Read Data Response Token
	    // Response byte format: xxx0sss1 (Bit 0 = 1, Bits 3:1 = status)
	    // 0x05 (0b00101) = Data accepted
	    uint8_t resp = SPI_TxRx(0xFF);
	    if ((resp & 0x1F) != 0x05)
	    {
	      SD_CS_HIGH();
	      SPI_TxRx(0xFF);
	      return RES_ERROR;
	    }

	    // 6. Wait while SD card writes data to flash (busy signal: line held low)
	    uint32_t timeout = HAL_GetTick() + 500;
	    while (SPI_TxRx(0xFF) == 0x00)
	    {
	      if (HAL_GetTick() > timeout)
	      {
	        SD_CS_HIGH();
	        SPI_TxRx(0xFF);
	        return RES_ERROR;
	      }
	    }

	    SD_CS_HIGH();
	    SPI_TxRx(0xFF);

	    buff += 512;
	    if (CardType == 4) sector += 1;
	    else sector += 512;
	  }

  /* USER CODE HERE */
    return RES_OK;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
    DRESULT res = RES_ERROR;
    if (pdrv != 0) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    switch (cmd)
    {
      case CTRL_SYNC:
        res = RES_OK;
        break;
      case GET_SECTOR_SIZE:
        *(WORD*)buff = 512;
        res = RES_OK;
        break;
      case GET_BLOCK_SIZE:
        *(DWORD*)buff = 1;
        res = RES_OK;
        break;
      default:
        res = RES_PARERR;
        break;
    }

    return res;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

