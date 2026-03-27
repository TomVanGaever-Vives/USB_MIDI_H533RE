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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tusb.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
//--------------------------------------------------------------------+
// MCP23S17 SPI Keypad Configuration
//--------------------------------------------------------------------+
#define MCP23S17_ADDR   0x40
#define MCP_CS_PORT     GPIOA
#define MCP_CS_PIN      GPIO_PIN_5

#define MCP_IODIRA    0x00
#define MCP_IODIRB    0x01
#define MCP_GPPUA     0x0C
#define MCP_GPPUB     0x0D
#define MCP_REG_GPIOA 0x12
#define MCP_REG_GPIOB 0x13

// Bit-bang SPI GPIO pins
#define SPI_SCK_PORT    GPIOB
#define SPI_SCK_PIN     GPIO_PIN_13
#define SPI_MISO_PORT   GPIOB
#define SPI_MISO_PIN    GPIO_PIN_14
#define SPI_MOSI_PORT   GPIOB
#define SPI_MOSI_PIN    GPIO_PIN_15

// Debounce
#define DEBOUNCE_COUNT 5

// Potentiometer ADC
#define HYSTERESIS 4
#define MIDI_CC_POT1 16
#define MIDI_CC_POT2 17
#define ADC_MIN 50    // Actual minimum ADC value from pot
#define ADC_MAX 254   // Actual maximum ADC value from pot

enum {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED     = 1000,
  BLINK_SUSPENDED   = 2500,
};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
ADC_HandleTypeDef hadc1;
DMA_NodeTypeDef Node_GPDMA1_Channel0;
DMA_QListTypeDef List_GPDMA1_Channel0;
DMA_HandleTypeDef handle_GPDMA1_Channel0;

TIM_HandleTypeDef htim6;

PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* USER CODE BEGIN PV */
// Matrix state buffers
uint8_t keypad_state[4]     = {0x0F, 0x0F, 0x0F, 0x0F};
uint8_t keypad_prev[4]      = {0x0F, 0x0F, 0x0F, 0x0F};
static uint8_t keypad_candidate[4]    = {0x0F, 0x0F, 0x0F, 0x0F};
static uint8_t keypad_stable_cnt[4]   = {0, 0, 0, 0};

const uint8_t note_map[4][4] = {
  {60, 62, 64, 65},
  {67, 69, 71, 72},
  {74, 76, 77, 79},
  {81, 83, 84, 86}
};

static uint8_t active_notes[4][4] = {0};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// ADC Potentiometer (filled by DMA)
volatile uint8_t adc_values[2] = {0, 0};  // [0]=PA0 (pot1), [1]=PA1 (pot2)
static uint8_t last_midi_value_pot1 = 0;
static uint8_t last_midi_value_pot2 = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_GPDMA1_Init(void);
static void MX_USB_PCD_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */
static void MX_SPI_BitBang_Init(void);
static uint8_t spi_transfer_byte(uint8_t data);

static void mcp23s17_cs_low(void);
static void mcp23s17_cs_high(void);
static void mcp23s17_write_reg(uint8_t reg, uint8_t value);
static uint8_t mcp23s17_read_reg(uint8_t reg);
static void mcp23s17_init(void);

static void keypad_scan(void);
static void keypad_task(void);

void led_blinking_task(void);
void midi_task(void);
void ADC_Start(void);
void process_potentiometer(void);
void process_potentiometer_2(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static inline uint32_t board_millis(void)
{
  return HAL_GetTick();
}

static inline void board_led_write(bool state)
{
  (void)state; // PA5 is MCP CS
}

static inline void board_init_after_tusb(void) { (void)0; }

// TinyUSB example used BOARD_TUD_RHPORT, on STM32 FS device this is 0.
#ifndef BOARD_TUD_RHPORT
  #define BOARD_TUD_RHPORT 0
#endif
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
  MX_GPDMA1_Init();
  MX_USB_PCD_Init();
  MX_ADC1_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
  MX_SPI_BitBang_Init();
  mcp23s17_init();
  
  /* Start ADC with DMA for potentiometer reading */
  ADC_Start();
  
  /* Initialize TinyUSB stack */
  tusb_rhport_init_t dev_init = {
    .role  = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_AUTO
  };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  
  board_init_after_tusb();
  /* USER CODE END 2 */

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USB stack must be serviced first */
    tud_task();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    keypad_task();
    midi_task();
    process_potentiometer();
    process_potentiometer_2();
    led_blinking_task();
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_CSI;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  RCC_OscInitStruct.CSICalibrationValue = RCC_CSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_CSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 125;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_8B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_24CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Rank = ADC_REGULAR_RANK_2;
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief GPDMA1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPDMA1_Init(void)
{

  /* USER CODE BEGIN GPDMA1_Init 0 */

  /* USER CODE END GPDMA1_Init 0 */

  /* Peripheral clock enable */
  __HAL_RCC_GPDMA1_CLK_ENABLE();

  /* GPDMA1 interrupt Init */
    HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);

  /* USER CODE BEGIN GPDMA1_Init 1 */

  /* USER CODE END GPDMA1_Init 1 */
  /* USER CODE BEGIN GPDMA1_Init 2 */

  /* USER CODE END GPDMA1_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 249;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 999;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_PCD_Init(void)
{

  /* USER CODE BEGIN USB_Init 0 */

  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */

  /* USER CODE END USB_Init 1 */
  hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
  hpcd_USB_DRD_FS.Init.dev_endpoints = 8;
  hpcd_USB_DRD_FS.Init.speed = USBD_FS_SPEED;
  hpcd_USB_DRD_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_DRD_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.battery_charging_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.bulk_doublebuffer_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.iso_singlebuffer_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_DRD_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */
  // CRITICAL: ST's Middleware usually starts the PCD. Since we use TinyUSB, we must start it manually!
  // Without this, the USB DP pull-up resistor is never enabled and the PC won't detect the device.
  HAL_PCD_Start(&hpcd_USB_DRD_FS);
  /* USER CODE END USB_Init 2 */

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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PC4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
//--------------------------------------------------------------------+
// ADC Potentiometer Control Change (CC) Handler
//--------------------------------------------------------------------+
// Start ADC and DMA sampling
void ADC_Start(void)
{
  // Start timer 6 (provides trigger for ADC)
  HAL_TIM_Base_Start(&htim6);
  
  // Start ADC with DMA (circular mode)
  // DMA will continuously fill adc_values buffer (2 channels: PA0 and PA1)
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_values, 2);
}

// Process potentiometer 1 (PA0) value and send MIDI CC if changed significantly
void process_potentiometer(void)
{
  // Remap ADC range (50-254) to MIDI range (0-127)
  uint8_t adc_raw = adc_values[0];
  uint8_t new_value;
  if (adc_raw < ADC_MIN)
    new_value = 0;
  else if (adc_raw > ADC_MAX)
    new_value = 127;
  else
    new_value = ((adc_raw - ADC_MIN) * 127) / (ADC_MAX - ADC_MIN);
  
  // Calculate absolute difference
  int16_t diff = (int16_t)new_value - (int16_t)last_midi_value_pot1;
  if (diff < 0) diff = -diff;
  
  // Only send if difference exceeds hysteresis threshold (reduces jitter)
  if (diff >= HYSTERESIS)
  {
    if (tud_midi_mounted())
    {
      // MIDI CC Message format (4 bytes for TinyUSB)
      uint8_t cc_msg[4] = {
        0x0B,           // CIN: Control Change
        0xB0,           // Status: CC on channel 0
        MIDI_CC_POT1,   // CC# 16 (potentiometer 1)
        new_value       // Value 0-127
      };
      tud_midi_packet_write(cc_msg);
    }
    
    // Update last value for next comparison
    last_midi_value_pot1 = new_value;
  }
}

// Process potentiometer 2 (PA1) value and send MIDI CC if changed significantly
void process_potentiometer_2(void)
{
  // Remap ADC range (50-254) to MIDI range (0-127)
  uint8_t adc_raw = adc_values[1];
  uint8_t new_value;
  if (adc_raw < ADC_MIN)
    new_value = 0;
  else if (adc_raw > ADC_MAX)
    new_value = 127;
  else
    new_value = ((adc_raw - ADC_MIN) * 127) / (ADC_MAX - ADC_MIN);
  
  // Calculate absolute difference
  int16_t diff = (int16_t)new_value - (int16_t)last_midi_value_pot2;
  if (diff < 0) diff = -diff;
  
  // Only send if difference exceeds hysteresis threshold (reduces jitter)
  if (diff >= HYSTERESIS)
  {
    if (tud_midi_mounted())
    {
      // MIDI CC Message format (4 bytes for TinyUSB)
      uint8_t cc_msg[4] = {
        0x0B,           // CIN: Control Change
        0xB0,           // Status: CC on channel 0
        MIDI_CC_POT2,   // CC# 17 (potentiometer 2)
        new_value       // Value 0-127
      };
      tud_midi_packet_write(cc_msg);
    }
    
    // Update last value for next comparison
    last_midi_value_pot2 = new_value;
  }
}

//--------------------------------------------------------------------+
// Device Callbacks - USB Connection State
//--------------------------------------------------------------------+
void tud_mount_cb(void) { blink_interval_ms = BLINK_MOUNTED; }
void tud_umount_cb(void) { blink_interval_ms = BLINK_NOT_MOUNTED; }
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; blink_interval_ms = BLINK_SUSPENDED; }
void tud_resume_cb(void) { blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED; }

//--------------------------------------------------------------------+
// Tasks and Drivers
//--------------------------------------------------------------------+
void midi_task(void)
{
  if (!tud_mounted()) return;
  while (tud_midi_available())
  {
    uint8_t packet[4];
    tud_midi_packet_read(packet);
  }
}

void keypad_task(void)
{
  static uint32_t scan_ms = 0;
  if (board_millis() - scan_ms < 20) return;
  scan_ms = board_millis();

  keypad_scan();

  if (!tud_mounted())
  {
    for (int i = 0; i < 4; i++) keypad_prev[i] = keypad_state[i];
    return;
  }

  uint8_t cable_num = 0;
  uint8_t channel = 0;

  for (int row = 0; row < 4; row++)
  {
    for (int col = 0; col < 4; col++)
    {
      uint8_t current_bit = (keypad_state[row] >> col) & 1;
      uint8_t prev_bit = (keypad_prev[row] >> col) & 1;

      // Note On
      if (prev_bit && !current_bit && !active_notes[row][col])
      {
        uint8_t note = note_map[col][row]; 
        uint8_t note_on[4] = {
          (uint8_t)((cable_num << 4) | 0x09),
          (uint8_t)(0x90 | channel),
          note,
          127
        };
        tud_midi_packet_write(note_on);
        active_notes[row][col] = note;
      }
      // Note Off
      else if (!prev_bit && current_bit && active_notes[row][col])
      {
        uint8_t note = active_notes[row][col];
        uint8_t note_off[4] = {
          (uint8_t)((cable_num << 4) | 0x08),
          (uint8_t)(0x80 | channel),
          note,
          0
        };
        tud_midi_packet_write(note_off);
        active_notes[row][col] = 0;
      }
    }
  }

  for (int i = 0; i < 4; i++) keypad_prev[i] = keypad_state[i];
}

void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;
  if (board_millis() - start_ms < blink_interval_ms) return;
  start_ms += blink_interval_ms;
  board_led_write(led_state);
  led_state = (bool)(!led_state);
}

static void MX_SPI_BitBang_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin   = SPI_SCK_PIN | SPI_MOSI_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin  = SPI_MISO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  HAL_GPIO_WritePin(SPI_SCK_PORT,  SPI_SCK_PIN,  GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_PIN_RESET);
}

// Microsecond timing: At 250MHz, each iteration ~10ns, so 100 loops ≈ 1μs
static void spi_delay(void) {
  for (volatile int d = 0; d < 100; d++) {
      __NOP();
  }
}

static uint8_t spi_transfer_byte(uint8_t data)
{
  uint8_t received = 0;
  for (int i = 7; i >= 0; i--)
  {
    if (data & (1u << i)) HAL_GPIO_WritePin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_PIN_SET);
    else HAL_GPIO_WritePin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_PIN_RESET);      

    spi_delay();
    HAL_GPIO_WritePin(SPI_SCK_PORT, SPI_SCK_PIN, GPIO_PIN_SET);
    spi_delay();

    if (HAL_GPIO_ReadPin(SPI_MISO_PORT, SPI_MISO_PIN) == GPIO_PIN_SET)        
      received |= (1u << i);

    HAL_GPIO_WritePin(SPI_SCK_PORT, SPI_SCK_PIN, GPIO_PIN_RESET);
    spi_delay();
  }
  return received;
}

static void mcp23s17_cs_low(void) { HAL_GPIO_WritePin(MCP_CS_PORT, MCP_CS_PIN, GPIO_PIN_RESET); spi_delay(); }
static void mcp23s17_cs_high(void) { spi_delay(); HAL_GPIO_WritePin(MCP_CS_PORT, MCP_CS_PIN, GPIO_PIN_SET); spi_delay(); }

static void mcp23s17_write_reg(uint8_t reg, uint8_t value)
{
  uint8_t opcode = MCP23S17_ADDR | 0x00;
  mcp23s17_cs_low();
  spi_transfer_byte(opcode);
  spi_transfer_byte(reg);
  spi_transfer_byte(value);
  mcp23s17_cs_high();
}

static uint8_t mcp23s17_read_reg(uint8_t reg)
{
  uint8_t opcode = MCP23S17_ADDR | 0x01;
  mcp23s17_cs_low();
  spi_transfer_byte(opcode);
  spi_transfer_byte(reg);
  uint8_t result = spi_transfer_byte(0x00);
  mcp23s17_cs_high();
  return result;
}

static void mcp23s17_init(void)
{
  mcp23s17_write_reg(MCP_IODIRA, 0x00);
  mcp23s17_write_reg(MCP_GPPUA,  0x00);
  mcp23s17_write_reg(MCP_IODIRB, 0x0F);
  mcp23s17_write_reg(MCP_GPPUB,  0x0F);
  mcp23s17_write_reg(MCP_REG_GPIOA, 0x0F);

  for (int i = 0; i < 4; i++)
  {
    keypad_state[i]      = 0x0F;
    keypad_prev[i]       = 0x0F;
    keypad_candidate[i]  = 0x0F;
    keypad_stable_cnt[i] = 0;
  }
}

static void keypad_scan(void)
{
  for (int row = 0; row < 4; row++)
  {
    uint8_t row_pattern = ~(1 << row) & 0x0F;
    mcp23s17_write_reg(MCP_REG_GPIOA, row_pattern);

    uint8_t read1 = mcp23s17_read_reg(MCP_REG_GPIOB) & 0x0F;
    uint8_t read2 = mcp23s17_read_reg(MCP_REG_GPIOB) & 0x0F;

    if (read1 != read2)
    {
      keypad_stable_cnt[row] = 0;
      continue;
    }

    if (read1 == keypad_candidate[row])
    {
      if (keypad_stable_cnt[row] < DEBOUNCE_COUNT)
        keypad_stable_cnt[row]++;
      if (keypad_stable_cnt[row] >= DEBOUNCE_COUNT)
        keypad_state[row] = read1;
    }
    else
    {
      keypad_candidate[row]  = read1;
      keypad_stable_cnt[row] = 1;
    }
  }

  mcp23s17_write_reg(MCP_REG_GPIOA, 0x0F);
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
