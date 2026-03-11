/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "tusb.h"
#include "stm32h5xx_nucleo.h"   // BSP LED helpers
// Note: SPI HAL driver is not included in this project, so we use bit-bang SPI via GPIO

//--------------------------------------------------------------------+
// HAL / CubeMX prototypes
//--------------------------------------------------------------------+
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USB_Init(void);
static void MX_SPI_BitBang_Init(void);

//--------------------------------------------------------------------+
// MCP23S17 SPI Keypad Configuration
//--------------------------------------------------------------------+
// MCP23S17: 16-pin SPI I/O expander
// Hardwired I2C address: 0x40 (A0, A1, A2 all connected to GND)
//
// GPIO Mapping:
//   GPIOA (bits 0-3): Kolommen = outputs (C1-C4)  ← gedreven om matrix te scannen
//   GPIOB (bits 0-3): Rijen   = inputs + pull-ups (R1-R4)  ← gelezen bij scanning
//
// SPI Pins:
//   PA5  = CS   (Chip Select)
//   PB13 = SCK  (SPI Clock)
//   PB15 = MOSI (SI van MCP23S17)
//   PB14 = MISO (SO van MCP23S17)
//
#define MCP23S17_ADDR   0x40  // Hardwired address (A0, A1, A2 = GND)
#define MCP_CS_PORT     GPIOA
#define MCP_CS_PIN      GPIO_PIN_5

// MCP23S17 Register Addresses (Sequential Mode)
#define MCP_IODIRA    0x00  // I/O Direction Register A (0=output, 1=input)
#define MCP_IODIRB    0x01  // I/O Direction Register B
#define MCP_GPPUA     0x0C  // GPIO Pull-Up Resistor A (1=enabled)
#define MCP_GPPUB     0x0D  // GPIO Pull-Up Resistor B
#define MCP_REG_GPIOA 0x12  // GPIO Port A
#define MCP_REG_GPIOB 0x13  // GPIO Port B

// Bit-bang SPI GPIO pins
// PB13 = SCK (Clock), PB15 = MOSI (Data Out), PB14 = MISO (Data In)
#define SPI_SCK_PORT    GPIOB
#define SPI_SCK_PIN     GPIO_PIN_13
#define SPI_MISO_PORT   GPIOB
#define SPI_MISO_PIN    GPIO_PIN_14
#define SPI_MOSI_PORT   GPIOB
#define SPI_MOSI_PIN    GPIO_PIN_15

// Matrix state buffers
uint8_t keypad_state[4]     = {0x0F, 0x0F, 0x0F, 0x0F};  // Geaccepteerde stabiele state
uint8_t keypad_prev[4]      = {0x0F, 0x0F, 0x0F, 0x0F};  // Vorige stabiele state

// Debounce: eis 5 identieke scans op rij voor een state-wijziging
// 5 x 20ms = 100ms stabiliteitsvenster
// Bewegende draden produceren nooit 100ms lang dezelfde ruis-waarde
#define DEBOUNCE_COUNT 5
static uint8_t keypad_candidate[4]    = {0x0F, 0x0F, 0x0F, 0x0F};  // Kandidaat waarde
static uint8_t keypad_stable_cnt[4]   = {0, 0, 0, 0};              // Teller per rij

// MIDI Note Mapping Table
// Chromatische schaal: C majeur getraponeerd over 4 octaven
// note_map[row][col] = MIDI noot nummer (0-127)
//
// Matrix layout:
//   Rij 0 (GPB0 laag): C4(60), D4(62), E4(64), F4(65)
//   Rij 1 (GPB1 laag): G4(67), A4(69), B4(71), C5(72)
//   Rij 2 (GPB2 laag): D5(74), E5(76), F5(77), G5(79)
//   Rij 3 (GPB3 laag): A5(81), B5(83), C6(84), D6(86)
//
const uint8_t note_map[4][4] = {
  {60, 62, 64, 65},  // Rij 1: C4 - F4
  {67, 69, 71, 72},  // Rij 2: G4 - C5
  {74, 76, 77, 79},  // Rij 3: D5 - G5
  {81, 83, 84, 86}   // Rij 4: A5 - D6
};

// Bit-bang SPI functions
static uint8_t spi_transfer_byte(uint8_t data);             // Stuur/ontvang 1 byte

// MCP23S17 SPI Driver Functions
static void mcp23s17_write_reg(uint8_t reg, uint8_t value);  // Schrijf register via SPI
static uint8_t mcp23s17_read_reg(uint8_t reg);              // Lees register via SPI
static void mcp23s17_init(void);                             // Initialiseer MCP23S17

// Keypad Scanning Functions
static void keypad_scan(void);     // Scant matrix (drive rijen, lees kolommen)
static void keypad_task(void);     // Detecteert indrukken/loslaten, stuurt MIDI

// USB Device Core handle
PCD_HandleTypeDef hpcd_USB_DRD_FS;

//--------------------------------------------------------------------+
// Minimal replacements for TinyUSB example "board_*" functions
//--------------------------------------------------------------------+
static inline uint32_t board_millis(void)
{
  return HAL_GetTick();
}

static inline void board_led_write(bool state)
{
  // LED2 (PA5) is NOT used: PA5 is wired to MCP23S17 CS.
  // Calling BSP_LED_On/Off would corrupt the chip select line.
  (void)state;
}

// On the original example these exist, keep them as no-ops for compatibility
static inline void board_init_after_tusb(void) { (void)0; }

// TinyUSB example used BOARD_TUD_RHPORT, on STM32 FS device this is 0.
#ifndef BOARD_TUD_RHPORT
  #define BOARD_TUD_RHPORT 0
#endif

/* This MIDI example send sequence of note (on/off) repeatedly. To test on PC, you need to install
 * synth software and midi connection management software. On
 * - Linux (Ubuntu): install qsynth, qjackctl. Then connect TinyUSB output port to FLUID Synth input port
 * - Windows: install MIDI-OX
 * - MacOS: SimpleSynth
 */

//--------------------------------------------------------------------+
// Configuration: LED Blink Intervals
//--------------------------------------------------------------------+
// LED feedback patterns voor USB connection state
// Blink snelheid geeft aan wat de USB status is
//
// Interval values (milliseconden):
//   250ms  = Slow blink   → Device NOT connected to USB host
//   1000ms = Medium blink → Device connected to USB host (mounted)
//   2500ms = Slow blink   → USB bus suspended (power saving mode)
//
enum {
  BLINK_NOT_MOUNTED = 250,    // No USB host connected
  BLINK_MOUNTED     = 1000,   // USB host connected (host detected)
  BLINK_SUSPENDED   = 2500,   // USB suspended (low power mode)
};

// Current LED blink interval (changed by USB callbacks)
static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void led_blinking_task(void);
void midi_task(void);

//--------------------------------------------------------------------+
// MAIN APPLICATION ENTRY POINT
//--------------------------------------------------------------------+
int main(void)
{
  // ===== Hardware Initialization =====
  HAL_Init();                  // STM32 Hardware Abstraction Layer init
  SystemClock_Config();        // Configure system clock
  MX_GPIO_Init();              // GPIO init (includes CS pin setup)
  MX_SPI_BitBang_Init();       // Configure bit-bang SPI GPIO pins for MCP23S17
  MX_USB_Init();               // USB Device peripheral init

  // NOTE: PA5 = MCP23S17 CS, PB13 = SCK, PB15 = MOSI, PB14 = MISO

  // ===== MCP23S17 Keypad Initialization =====
  // Configure GPIOA as inputs (kolommen) met pull-ups
  // Configure GPIOB as outputs (rijen) voor matrix scanning
  mcp23s17_init();

  // ===== USB MIDI Device Stack =====
  // Initialize TinyUSB as MIDI device
  tusb_rhport_init_t dev_init = {
    .role  = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_AUTO
  };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);

  board_init_after_tusb();

  // ===== Main Application Loop =====
  while (1)
  {
    tud_task();              // USB stack servicing (zo vaak mogelijk aanroepen!)
    keypad_task();           // Matrix scan + MIDI Note On/Off
    midi_task();             // Inkomende MIDI legen + auto test-noot
    led_blinking_task();     // LED status (no-op: PA5=SCK, geen conflict)
  }
}

//--------------------------------------------------------------------+
// Device Callbacks - USB Connection State
//--------------------------------------------------------------------+

// Invoked when device is mounted (USB host detected)
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted (USB host disconnected)
// Used for LED feedback: slow blink = not mounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when USB bus is suspended
// Device must reduce current draw to <2.5mA
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when USB bus is resumed
// Device can resume normal operation
void tud_resume_cb(void)
{
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// MIDI Task - USB MIDI Traffic Handler
//--------------------------------------------------------------------+
// Deze functie verwerkt inkomende MIDI data van de USB host
// (bijv. MIDI clock, control change messages, etc.)
//
// Voor dit keyboard: we ontvangen maar ignoren (read-only instrument)
//
void midi_task(void)
{
  if (!tud_mounted()) return;

  // Lees en verwerp inkomende MIDI packets (voorkomt volle buffer)
  while (tud_midi_available())
  {
    uint8_t packet[4];
    tud_midi_packet_read(packet);
  }

  // Automatische C3 test-noot elke 2 seconden - UITGESCHAKELD
  /*
  static uint32_t test_ms = 0;
  static bool note_on_sent = false;
  uint32_t now = board_millis();
  if (!note_on_sent && (now - test_ms) >= 2000u)
  {
    uint8_t on[4] = { 0x09, 0x90, 60, 100 };
    tud_midi_packet_write(on);
    note_on_sent = true;
    test_ms = now;
  }
  else if (note_on_sent && (now - test_ms) >= 200u)
  {
    uint8_t off[4] = { 0x08, 0x80, 60, 0 };
    tud_midi_packet_write(off);
    note_on_sent = false;
    test_ms = now;
  }
  */
}

//--------------------------------------------------------------------+
// Keypad Scanning Task - Matrix Scan & MIDI Generation
//--------------------------------------------------------------------+
// Deze functie:
// 1. Scant de 4x4 matrix elke 20ms (debouncing interval)
// 2. Vergelijkt huidige state met vorige state
// 3. Detecteert toets indrukken (1→0 transitie)
// 4. Detecteert toets loslaten (0→1 transitie)
// 5. Stuurt MIDI Note On/Off naar USB Host
//
// State representation:
//   keypad_state[row][col] bit:
//   - 0 = toets ingedrukt (GPB row low + GPA col low)
//   - 1 = toets los (pull-ups opgetrokken)
//

static uint8_t active_notes[4][4] = {0};  // Track which notes are active (velocity=127)

void keypad_task(void)
{
  // Debouncing: Scan slechts elke 20ms
  static uint32_t scan_ms = 0;
  if (board_millis() - scan_ms < 20)
  {
    return;
  }
  scan_ms = board_millis();

  // Scant de matrix (drive one row low, read columns)
  // Scanning loopt altijd, ook als USB niet gemount is (state bijhouden)
  keypad_scan();

  // MIDI alleen sturen als USB host aangesloten is
  if (!tud_mounted())
  {
    // State wel bijwerken zodat na mount geen valse edge detection
    for (int i = 0; i < 4; i++) keypad_prev[i] = keypad_state[i];
    return;
  }

  // ===== MIDI Configuration =====
  uint8_t cable_num = 0;
  uint8_t channel = 0;

  // ===== Matrix state change detection =====
  for (int row = 0; row < 4; row++)
  {
    for (int col = 0; col < 4; col++)
    {
      // Extract bit for this key
      uint8_t current_bit = (keypad_state[row] >> col) & 1;
      uint8_t prev_bit = (keypad_prev[row] >> col) & 1;

      // ===== KEY PRESSED (1→0 transition) =====
      // Vorige state = 1 (los), huidige state = 0 (ingedrukt)
      if (prev_bit && !current_bit && !active_notes[row][col])
      {
        uint8_t note = note_map[col][row];  // col=physical row, row=physical col (scan drives columns)

        // Build MIDI Note On packet
        // Format: [cable+status, status_byte, data1, data2]
        // Status 0x09 = Note On in CIN (Code Index Number)
        uint8_t note_on[4] = {
          (uint8_t)((cable_num << 4) | 0x09),  // Cable 0, CIN 9 (Note On)
          (uint8_t)(0x90 | channel),            // Note On message + channel
          note,                                  // MIDI note number
          127                                    // Velocity (maximum)
        };
        tud_midi_packet_write(note_on);
        active_notes[row][col] = note;  // Remember which note is active
      }

      // ===== KEY RELEASED (0→1 transition) =====
      // Vorige state = 0 (ingedrukt), huidige state = 1 (los)
      else if (!prev_bit && current_bit && active_notes[row][col])
      {
        uint8_t note = active_notes[row][col];

        // Build MIDI Note Off packet
        // Status 0x08 = Note Off in CIN (Code Index Number)
        uint8_t note_off[4] = {
          (uint8_t)((cable_num << 4) | 0x08),  // Cable 0, CIN 8 (Note Off)
          (uint8_t)(0x80 | channel),            // Note Off message + channel
          note,                                  // MIDI note number
          0                                      // Velocity (ignored for Note Off)
        };
        tud_midi_packet_write(note_off);
        active_notes[row][col] = 0;  // Mark note as inactive
      }
    }
  }

  // ===== Debug: stuur CC per rij met raw GPIOA waarde =====
  // Tijdelijk uitgeschakeld - eerst basis werkend krijgen
  /*
  for (int row = 0; row < 4; row++)
  {
    uint8_t raw_val = (~keypad_state[row]) & 0x0F;
    uint8_t cc[4] = { 0x0B, 0xB0, (uint8_t)(20 + row), raw_val };
    tud_midi_packet_write(cc);
  }
  */

  // ===== Update state for next cycle =====
  for (int i = 0; i < 4; i++)
  {
    keypad_prev[i] = keypad_state[i];
  }
}

//--------------------------------------------------------------------+
// LED Blinking Task
//--------------------------------------------------------------------+
// Blinkt on-board LED (LED2 = PA5) als USB status feedback
//
// Blink patterns:
//   - 250ms interval: Not mounted (no USB connection)
//   - 1000ms interval: Mounted (USB host connected)
//   - 2500ms interval: Suspended (USB bus suspended)
//
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Check if it's time to toggle LED
  // blink_interval_ms is set by tud_mount_cb, tud_umount_cb, tud_suspend_cb
  if (board_millis() - start_ms < blink_interval_ms)
  {
    return;  // Not yet time to toggle
  }
  
  start_ms += blink_interval_ms;  // Update timestamp

  // Toggle LED
  board_led_write(led_state);
  led_state = (bool)(!led_state);
}

//--------------------------------------------------------------------+
// GPIO Initialization
//--------------------------------------------------------------------+
// Configureert alle GPIO poorten en chip select pin
//
static void MX_GPIO_Init(void)
{
  // Enable clocks for all GPIO ports
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  // ===== MCP23S17 Chip Select Pin (PA5) =====
  // SPI standard: CS low = chip selected, CS high = chip not selected
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_5;                        // PA5 = CS
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);      // CS initially HIGH (inactive)
}

//--------------------------------------------------------------------+
// SPI1 Initialization for MCP23S17 Communication
//--------------------------------------------------------------------+
// Configureert SPI1 master mode voor serial communicatie met MCP23S17
//
// Hardware connections:
//   PA5  = CS  (Chip Select)
//   PB13 = SCK (Serial Clock)
//   PB15 = MOSI (Master Out Slave In / SI van MCP23S17)
//   PB14 = MISO (Master In Slave Out / SO van MCP23S17)
//
// SPI Protocol:
//   - Mode: SPI Master, Mode 0 (CPOL=0, CPHA=0)
//   - Data width: 8-bit
//   - MSB first (Most Significant Bit first)
//
//--------------------------------------------------------------------+
// Bit-bang SPI Initialization
//--------------------------------------------------------------------+
// Configureert GPIO pins handmatig als SPI (geen SPI hardware driver nodig)
// SPI Mode 0: CPOL=0 (clock idle LOW), CPHA=0 (sample on rising edge)
// MSB first, ~100kHz effectieve snelheid (genoeg voor MCP23S17)
//
// Pins:
//   PB13 = SCK  (output)
//   PB15 = MOSI (output)
//   PB14 = MISO (input)
//
static void MX_SPI_BitBang_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // ===== SCK (PB13) + MOSI (PB15) als output =====
  GPIO_InitStruct.Pin   = SPI_SCK_PIN | SPI_MOSI_PIN;      // PB13 en PB15 samen
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;              // Push-pull output
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // ===== MISO (PB14) als input =====
  GPIO_InitStruct.Pin  = SPI_MISO_PIN;                      // PB14
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;                       // Pull-up voor idle state
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // ===== Initiële states =====
  HAL_GPIO_WritePin(SPI_SCK_PORT,  SPI_SCK_PIN,  GPIO_PIN_RESET); // SCK = LOW (idle)
  HAL_GPIO_WritePin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_PIN_RESET); // MOSI = LOW
}

//--------------------------------------------------------------------+
// Bit-bang SPI Byte Transfer
//--------------------------------------------------------------------+
// Stuurt 1 byte via MOSI en ontvangt tegelijkertijd 1 byte via MISO
// Protocol: MSB first, Mode 0 (CPOL=0, CPHA=0)
//   - Data gezet voor stijgende flank van SCK
//   - Data gelezen op stijgende flank van SCK
//
static uint8_t spi_transfer_byte(uint8_t data)
{
  uint8_t received = 0;

  for (int i = 7; i >= 0; i--)  // MSB first (bit 7 down to bit 0)
  {
    // ===== Zet MOSI bit =====
    if (data & (1u << i))
      HAL_GPIO_WritePin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_PIN_SET);
    else
      HAL_GPIO_WritePin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_PIN_RESET);

    // ===== SCK stijgende flank → MCP23S17 samples MOSI =====
    HAL_GPIO_WritePin(SPI_SCK_PORT, SPI_SCK_PIN, GPIO_PIN_SET);

    // ===== Lees MISO (MCP23S17 drijft SO bij stijgende flank) =====
    if (HAL_GPIO_ReadPin(SPI_MISO_PORT, SPI_MISO_PIN) == GPIO_PIN_SET)
      received |= (1u << i);

    // ===== SCK dalende flank =====
    HAL_GPIO_WritePin(SPI_SCK_PORT, SPI_SCK_PIN, GPIO_PIN_RESET);
  }

  return received;
}
static void MX_USB_Init(void)
{
  // ===== Initialize USB Device peripheral =====
  // Configure USB DRD (Dual Role Device) in device mode (MIDI device)
  hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
  hpcd_USB_DRD_FS.Init.dev_endpoints = 8;                  // 8 endpoints available
  hpcd_USB_DRD_FS.Init.speed = PCD_SPEED_FULL;             // Full Speed (12 Mbps)
  hpcd_USB_DRD_FS.Init.phy_itface = PCD_PHY_EMBEDDED;      // Embedded PHY
  hpcd_USB_DRD_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.battery_charging_enable = DISABLE;
  
  if (HAL_PCD_Init(&hpcd_USB_DRD_FS) != HAL_OK)
  {
    Error_Handler();
  }

  // ===== Enable USB Interrupt =====
  // USB interrupt voor TinyUSB stack communicatie
  HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 0, 0);  // Highest priority
  HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}

//--------------------------------------------------------------------+
// System Clock Configuration
//--------------------------------------------------------------------+
// Configureert systemklok op STM32H5 voor USB compatibiliteit
// USB vereist precise timing voor MIDI communicatie
//
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  // ===== Voltage Scaling =====
  // VOS3 = 1.8V (low performance, low power)
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  // ===== Oscillator Configuration =====
  // STM32H5: HSI (8MHz) + HSI48 voor USB
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48 | RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV2;                 // HSI /2 = 4MHz
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;             // 48MHz clock (USB)
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;           // No PLL needed
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  // ===== Clock Distribution =====
  // HCLK = 4MHz (system clock)
  // PCLK1 = 4MHz (APB1)
  // PCLK2 = 4MHz (APB2)
  // PCLK3 = 4MHz (APB3)
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                              | RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;       // No AHB divider
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

//--------------------------------------------------------------------+
// Error Handler
//--------------------------------------------------------------------+
// Wordt aangeroepen wanneer een kritical error optreedt
// Disables interrupts en enters infinite loop
// Dit voorkomt verder execution van tainted code
//
void Error_Handler(void)
{
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();  // Disable all interrupts
  
  while (1)
  {
    // Infinite loop - watchdog (if enabled) will reset the system
    // Or LED blink pattern could be added here for debugging
  }
}

//--------------------------------------------------------------------+
// MCP23S17 SPI Driver Implementation
//--------------------------------------------------------------------+
// Low-level SPI functions voor communicatie met MCP23S17

// ===== Chip Select Control =====
// CS moet LOW zijn voor SPI transactie, HIGH daarna
static void mcp23s17_cs_low(void)
{
  HAL_GPIO_WritePin(MCP_CS_PORT, MCP_CS_PIN, GPIO_PIN_RESET);
}

static void mcp23s17_cs_high(void)
{
  HAL_GPIO_WritePin(MCP_CS_PORT, MCP_CS_PIN, GPIO_PIN_SET);
}

// ===== Register Write =====
// Schrijft een 8-bit waarde naar een MCP23S17 register via SPI
// Protocol: TX opcode (0x40)→ TX register address → TX data value
static void mcp23s17_write_reg(uint8_t reg, uint8_t value)
{
  // Opcode frame:
  // Bit 7: 0 = write
  // Bit 6-1: Device address (0x40 = all address bits low)
  // Bit 0: part of address
  uint8_t opcode = MCP23S17_ADDR | 0x00;  // Write mode (bit 0 = 0)

  mcp23s17_cs_low();
  spi_transfer_byte(opcode);   // opcode
  spi_transfer_byte(reg);      // register address
  spi_transfer_byte(value);    // data value
  mcp23s17_cs_high();
  // No HAL_Delay: bit-bang SPI is already slow enough, delay here blocks USB stack
}

// ===== Register Read =====
// Leest een 8-bit waarde uit een MCP23S17 register via SPI
// Protocol: TX opcode (0x41) → TX register address → RX data value
static uint8_t mcp23s17_read_reg(uint8_t reg)
{
  // Opcode frame:
  // Bit 7: 1 = read
  // Bit 6-1: Device address (0x40 = all address bits low)
  // Bit 0: part of address
  uint8_t opcode = MCP23S17_ADDR | 0x01;  // Read mode (bit 0 = 1)

  mcp23s17_cs_low();
  spi_transfer_byte(opcode);          // opcode
  spi_transfer_byte(reg);             // register address
  uint8_t result = spi_transfer_byte(0x00); // dummy write, capture MCP response
  mcp23s17_cs_high();
  // No HAL_Delay: bit-bang SPI is already slow enough, delay here blocks USB stack

  return result;  // Return received data byte
}

// ===== MCP23S17 Initialization =====
// ===== GPIOA = kolommen (outputs, gedreven), GPIOB = rijen (inputs + pull-ups) =====
static void mcp23s17_init(void)
{
  // ===== Configure GPIOA (Kolommen = outputs, gedreven voor matrix scan) =====
  mcp23s17_write_reg(MCP_IODIRA, 0x00);      // GPA0-GPA3 = outputs
  mcp23s17_write_reg(MCP_GPPUA,  0x00);      // GPA0-GPA3 = geen pull-ups (outputs)

  // ===== Configure GPIOB (Rijen = inputs + pull-ups) =====
  mcp23s17_write_reg(MCP_IODIRB, 0x0F);      // GPB0-GPB3 = inputs
  mcp23s17_write_reg(MCP_GPPUB,  0x0F);      // GPB0-GPB3 = pull-ups enabled
  mcp23s17_write_reg(MCP_REG_GPIOA, 0x0F);  // Alle kolommen HIGH (inactief)

  // ===== Initialize State Arrays =====
  for (int i = 0; i < 4; i++)
  {
    keypad_state[i]      = 0x0F;
    keypad_prev[i]       = 0x0F;
    keypad_candidate[i]  = 0x0F;
    keypad_stable_cnt[i] = 0;
  }
}

// ===== Matrix Scanning Function =====
// Scans 4x4 matrix by:
// 1. Driving one column LOW at a time (output GPA0-3)
// 2. Reading which rows go LOW (input GPB0-3, pulled up)
// 3. If column i and row j both low → key[j][i] is pressed
//
// After scan, all columns returned to HIGH (inactive)
static void keypad_scan(void)
{
  for (int row = 0; row < 4; row++)
  {
    // Drijf enkel deze kolom laag via GPIOA, andere kolommen hoog
    uint8_t row_pattern = ~(1 << row) & 0x0F;
    mcp23s17_write_reg(MCP_REG_GPIOA, row_pattern);

    // Dubbele lezing van GPIOB (rijen)
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

  // Zet alle kolommen terug hoog (inactief) via GPIOA
  mcp23s17_write_reg(MCP_REG_GPIOA, 0x0F);
}


