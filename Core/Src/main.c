/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "fatfs.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

SPI_HandleTypeDef hspi2;
SPI_HandleTypeDef hspi3;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#define WINDOW_PERFECT_MS 50
#define WINDOW_GOOD_MS    120

FIL MyFile;
uint8_t rtext[64];

typedef enum{
	NOTE_UNHIT = 0,
	NOTE_HIT,
	NOTE_MISSED
} NoteStatus_t;

typedef struct{
	uint32_t timestamp_ms;
	uint8_t fret_id;
	NoteStatus_t status;
} Note_t;

static Note_t song_track[] = {
  { 3000,  0, NOTE_UNHIT },
  { 5000,  0, NOTE_UNHIT },
  { 7000,  0, NOTE_UNHIT },
  { 9000,  0, NOTE_UNHIT },
  { 11000, 0, NOTE_UNHIT }
};

#define TRACK_NOTE_COUNT (sizeof(song_track) / sizeof(Note_t))

static uint32_t song_start_time = 0;
static uint8_t is_playing = 0;
static uint32_t current_score =0;
static uint32_t current_combo = 0;
static uint8_t song_finished = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI2_Init(void);
static void MX_SPI3_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch){
	HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
	return ch;
}


void Process_Button_Press(uint32_t press_time_abs){
	if(!is_playing) return;

	uint32_t song_elapsed_ms = press_time_abs - song_start_time;

	for(size_t i = 0; i < TRACK_NOTE_COUNT; i++)
	{
		if(song_track[i].status != NOTE_UNHIT) continue;

		int32_t delta = (int32_t)song_elapsed_ms - (int32_t)song_track[i].timestamp_ms;

		if(delta < -WINDOW_GOOD_MS) break;

		if(abs(delta) <= WINDOW_GOOD_MS){
			song_track[i].status = NOTE_HIT;
			HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);

			if(abs(delta) <= WINDOW_PERFECT_MS){
				current_score += 100;
				++current_combo;
				printf("[PERFECT] Target: %u ms | Delta: %+d ms | Score: %u | Combo: %u\r\n",
               (unsigned int)song_track[i].timestamp_ms, (int)delta,
               (unsigned int)current_score, (unsigned int)current_combo);

			}
			else{
				current_score +=50;
				current_combo++;
				printf("[GOOD]    Target: %u ms | Delta: %+d ms | Score: %u | Combo: %u\r\n",
			               (unsigned int)song_track[i].timestamp_ms, (int)delta,
			               (unsigned int)current_score, (unsigned int)current_combo);


			}
			return;

		}
	}
	printf("[EXTRA TAP] at %u ms | Combo reset!\r\n", (unsigned int)song_elapsed_ms);
	current_combo = 0;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_SPI2_Init();
  MX_FATFS_Init();
  MX_SPI3_Init();
  /* USER CODE BEGIN 2 */
  printf("\r\n=== System Boot: Testing MicroSD Write & Read ===\r\n");

    FRESULT res;
    UINT bytesWritten = 0;
    UINT bytesRead = 0;
    const char writeData[] = "RHYTHM_ENGINE_READY_FROM_STM32\r\n";

    // 1. Mount the filesystem
    res = f_mount(&USERFatFS, (TCHAR const*)USERPath, 1);
    if (res != FR_OK)
    {
      printf("[SD ERROR] f_mount failed: %d\r\n", res);
    }
    else
    {
      printf("[SD OK] Card mounted successfully!\r\n");

      // 2. Create and open test.txt for writing (overwrites if it already exists)
      res = f_open(&MyFile, "test.txt", FA_CREATE_ALWAYS | FA_WRITE);
      if (res != FR_OK)
      {
        printf("[SD ERROR] f_open (write) failed: %d\r\n", res);
      }
      else
      {
        printf("[SD OK] 'test.txt' created on SD card!\r\n");

        // 3. Write string data to the file
        res = f_write(&MyFile, writeData, sizeof(writeData) - 1, &bytesWritten);
        if (res != FR_OK)
        {
          printf("[SD ERROR] f_write failed: %d\r\n", res);
        }
        else
        {
          printf("[SD OK] Wrote %u bytes to test.txt\r\n", (unsigned int)bytesWritten);
        }

        // 4. MUST close the file to flush the cache and write FAT directory entries
        f_close(&MyFile);
      }

      // 5. Read back the file to verify data integrity
      res = f_open(&MyFile, "test.txt", FA_READ);
      if (res == FR_OK)
      {
        memset(rtext, 0, sizeof(rtext));
        uint32_t t_start = HAL_GetTick();
        res = f_read(&MyFile, rtext, sizeof(rtext) - 1, &bytesRead);
        uint32_t t_elapsed = HAL_GetTick() - t_start;
        printf("[BENCHMARK] Read time: %lu ms\r\n", t_elapsed);
        if (res == FR_OK)
        {

          printf("[SD OK] Read back %u bytes:\r\n", (unsigned int)bytesRead);
          printf("----------------------------------------\r\n");
          printf("%s", (char *)rtext);
          printf("----------------------------------------\r\n");
        }
        f_close(&MyFile);
      }
    }
  printf("\r\n=== System Boot: Rhythm Engine Initialized ===\r\n");
  printf("Track loaded: %u notes.\r\n", (unsigned int)TRACK_NOTE_COUNT);
  printf("First note coming at 3000 ms! Get ready...\r\n");

  song_start_time = HAL_GetTick();
  is_playing = 1;

  uint16_t TX_Buffer[] = {0b000101100000};

  HAL_SPI_Transmit(&hspi3, (uint8_t *)TX_Buffer, 1, 1000);
  HAL_Delay(100);

  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED2);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	  if(is_playing){

		  uint32_t elapsed = HAL_GetTick() - song_start_time;

		  for(size_t i = 0; i <TRACK_NOTE_COUNT; i++){

			  if(song_track[i].status == NOTE_UNHIT &&
					  ((int32_t)elapsed - (int32_t)song_track[i].timestamp_ms) > WINDOW_GOOD_MS){
				  song_track[i].status = NOTE_MISSED;
				  current_combo = 0;
				  printf("[MISSED]  Target: %u ms passed unhit! Combo reset.\r\n",
						 (unsigned int)song_track[i].timestamp_ms);
			  }



		  }
		  if(!song_finished && elapsed > song_track[TRACK_NOTE_COUNT - 1].timestamp_ms + 2000){
			  is_playing = 0;
			  song_finished = 1;
			  printf("\r\n=== SONG COMPLETE ===\r\n");
			  printf("Final Score: %u\r\n", (unsigned int)current_score);
			  printf("Press Black Reset Button to replay.\r\n");
		  }

	  }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{

  /* USER CODE BEGIN SPI3_Init 0 */

  /* USER CODE END SPI3_Init 0 */

  /* USER CODE BEGIN SPI3_Init 1 */

  /* USER CODE END SPI3_Init 1 */
  /* SPI3 parameter configuration*/
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PC12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static uint32_t last_press_time = 0;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{

	if(GPIO_Pin == GPIO_PIN_13){
		uint32_t now = HAL_GetTick();

		if ((now - last_press_time) > 100){

			last_press_time = now;
			Process_Button_Press(now);
		}

	}


}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
	  HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
	  HAL_Delay(500);
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
