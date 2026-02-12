# USB MIDI Implementation - Step-by-Step Explanation

## Overview

This project implements a **USB MIDI device** on an **STM32H533RE** microcontroller (NUCLEO-H533RE board). It uses the **TinyUSB** open-source USB stack to present the STM32 as a standard USB MIDI instrument to a host PC. The device sends a Note On/Off message (Middle C) every second as a demonstration.

---

## Project Architecture

```
USB_MIDI2/
├── Core/
│   ├── Inc/
│   │   └── tusb_config.h           ← TinyUSB configuration (what USB classes to enable)
│   └── Src/
│       ├── main.c                  ← Application entry point + MIDI task logic
│       ├── usb_descriptors.c       ← USB descriptors (how the device identifies itself)
│       ├── tuue sb_port.c             ← Platform glbetween TinyUSB and STM32 HAL
│       ├── stm32h5xx_it.c          ← Interrupt handlers (USB IRQ lives here)
│       └── stm32h5xx_hal_msp.c     ← Low-level peripheral init (USB clocks, power, IRQ)
└── Middlewares/tinyusb/             ← TinyUSB library (handles all USB protocol details)
```

---

## Step 1: System Clock Configuration

**File:** `Core/Src/main.c` — `SystemClock_Config()` (line 139)

Before USB can work, the microcontroller needs a precise 48 MHz clock. USB Full-Speed requires exactly this frequency for proper timing of data on the bus.

```c
RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48 | RCC_OSCILLATORTYPE_HSI;
RCC_OscInitStruct.HSIState = RCC_HSI_ON;        // Main system clock (HSI at 32 MHz after /2)
RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;     // 48 MHz oscillator specifically for USB
```

Two oscillators are enabled:
- **HSI** (High-Speed Internal): 64 MHz divided by 2 = 32 MHz system clock for the CPU.
- **HSI48**: A dedicated 48 MHz oscillator used exclusively as the USB clock source. This is configured later in the MSP init (Step 2).

---

## Step 2: USB Hardware Initialization (Low-Level)

**File:** `Core/Src/stm32h5xx_hal_msp.c` — `HAL_PCD_MspInit()` (line 83)

This function is called automatically by the HAL when `HAL_PCD_Init()` runs. It sets up the electrical and clock infrastructure for USB:

```c
// 1. Select HSI48 (48 MHz) as the USB clock source
PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

// 2. Enable the USB power rail on the chip
HAL_PWREx_EnableVddUSB();

// 3. Turn on the USB peripheral clock (gate)
__HAL_RCC_USB_CLK_ENABLE();

// 4. Configure and enable the USB interrupt at highest priority
HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 0, 0);  // Priority 0 = highest
HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
```

**What each step does:**
1. **Clock routing** — The USB peripheral needs exactly 48 MHz. This connects HSI48 to the USB module.
2. **Power rail** — The STM32H5 has a separate power domain for USB I/O pins. This enables it.
3. **Peripheral clock gate** — The bus clock to the USB hardware block is enabled.
4. **Interrupt** — USB events (connection, data transfer, enumeration) generate hardware interrupts. Priority 0 ensures USB is never blocked by other interrupts.

---

## Step 3: USB Peripheral Controller Init

**File:** `Core/Src/main.c` — `MX_USB_PCD_Init()` (line 190)

This configures the STM32's built-in USB Device Controller (PCD = Peripheral Controller Driver):

```c
hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
hpcd_USB_DRD_FS.Init.dev_endpoints = 8;              // Up to 8 endpoints available
hpcd_USB_DRD_FS.Init.speed = USBD_FS_SPEED;          // Full-Speed (12 Mbit/s)
hpcd_USB_DRD_FS.Init.phy_itface = PCD_PHY_EMBEDDED;  // Use the chip's built-in USB PHY
hpcd_USB_DRD_FS.Init.Sof_enable = DISABLE;           // No Start-of-Frame callback needed
hpcd_USB_DRD_FS.Init.low_power_enable = DISABLE;
hpcd_USB_DRD_FS.Init.lpm_enable = DISABLE;            // No Link Power Management
hpcd_USB_DRD_FS.Init.battery_charging_enable = DISABLE;
hpcd_USB_DRD_FS.Init.vbus_sensing_enable = DISABLE;   // Always assume USB is connected
hpcd_USB_DRD_FS.Init.bulk_doublebuffer_enable = DISABLE;
hpcd_USB_DRD_FS.Init.iso_singlebuffer_enable = DISABLE;
HAL_PCD_Init(&hpcd_USB_DRD_FS);  // This triggers HAL_PCD_MspInit() from Step 2
```

**Key points:**
- **Full-Speed** (12 Mbit/s) — this is what USB MIDI requires. High-Speed is not needed.
- **Embedded PHY** — the STM32H5 has built-in USB transceiver hardware; no external chip needed.
- **VBUS sensing disabled** — the device doesn't monitor the 5V USB power line. It always assumes it's connected.

---

## Step 4: TinyUSB Stack Initialization

**File:** `Core/Src/main.c` — `main()` (lines 98-100)

```c
tusb_init();       // Initialize TinyUSB's internal state machine and device stack
tusb_hal_init();   // Platform-specific init (empty in this project — HAL already did everything)
```

`tusb_init()` does the following internally:
1. Initializes the TinyUSB device controller driver (DCD) for the STM32H5.
2. Sets up internal FIFO buffers for endpoint data transfer.
3. Prepares the MIDI class driver (because `CFG_TUD_MIDI = 1` in the config).
4. Connects the device to the USB bus (the host PC will now detect it).

After this call, the device is "live" on the USB bus and the host will begin **enumeration** (the process of asking the device "what are you?").

---

## Step 5: TinyUSB Configuration

**File:** `Core/Inc/tusb_config.h`

This header tells TinyUSB what to compile and how to behave:

```c
#define CFG_TUSB_MCU            OPT_MCU_STM32H5     // Target microcontroller family
#define CFG_TUSB_OS             OPT_OS_NONE          // Bare-metal (no RTOS)
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE      // USB Device mode (not Host)

#define CFG_TUD_ENDPOINT0_SIZE  64    // Control endpoint max packet size

// Only MIDI is enabled — all other USB classes are disabled
#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            1     // <-- MIDI enabled
#define CFG_TUD_AUDIO           0
#define CFG_TUD_VENDOR          0

// MIDI RX/TX FIFO buffer sizes (in bytes)
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64
```

**Key detail:** Only `CFG_TUD_MIDI` is set to `1`. This means TinyUSB only compiles and links the MIDI device class driver, keeping the firmware small.

---

## Step 6: USB Descriptors — How the Device Identifies Itself

**File:** `Core/Src/usb_descriptors.c`

When a USB device is plugged in, the host PC asks it a series of questions through **descriptor requests**. The device responds with data structures that describe what it is and what it can do.

### 6.1 Device Descriptor

This is the first thing the host reads. It identifies the device at the highest level:

```c
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),  // 18 bytes
    .bDescriptorType    = TUSB_DESC_DEVICE,            // "I am a device descriptor"
    .bcdUSB             = 0x0200,      // USB 2.0 compliant
    .bDeviceClass       = 0x00,        // Class defined at interface level (not device level)
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,          // Control endpoint packet size

    .idVendor           = 0xCAFE,      // Vendor ID (placeholder — not officially registered)
    .idProduct          = 0x4001,      // Product ID
    .bcdDevice          = 0x0100,      // Device version 1.00

    .iManufacturer      = 0x01,        // Index into string descriptor table
    .iProduct           = 0x02,        // Index into string descriptor table
    .iSerialNumber      = 0x03,        // Index into string descriptor table

    .bNumConfigurations = 0x01         // Only one configuration
};
```

**Key points:**
- `bDeviceClass = 0x00` means the class is defined per-interface (the MIDI interface declares itself as Audio class).
- `idVendor = 0xCAFE` is a placeholder. A real product would need a registered USB Vendor ID.
- The host uses `idVendor` + `idProduct` together to find and load the correct driver.

### 6.2 Configuration Descriptor

This tells the host what interfaces and endpoints the device has:

```c
enum {
    ITF_NUM_MIDI = 0,           // Interface 0: Audio Control (required by USB Audio/MIDI spec)
    ITF_NUM_MIDI_STREAMING,     // Interface 1: MIDI Streaming (where actual MIDI data flows)
    ITF_NUM_TOTAL               // = 2 interfaces total
};

#define EPNUM_MIDI_OUT   0x01   // Endpoint 1 OUT — host sends MIDI data TO the device
#define EPNUM_MIDI_IN    0x81   // Endpoint 1 IN  — device sends MIDI data TO the host
                                // (0x80 bit = IN direction)

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
};
```

**What `TUD_MIDI_DESCRIPTOR` expands to (under the hood):**

The USB MIDI specification (part of USB Audio Class) requires a specific hierarchy:

```
Configuration
├── Interface 0: Audio Control (mandatory but essentially empty for MIDI-only)
│   └── AC Header descriptor
└── Interface 1: MIDI Streaming
    ├── MS Header descriptor
    ├── MIDI IN Jack (Embedded) — represents the device's input
    ├── MIDI OUT Jack (Embedded) — represents the device's output
    ├── Endpoint 0x01 OUT (Bulk, 64 bytes) — host-to-device data pipe
    │   └── MS Endpoint descriptor (links to MIDI jack)
    └── Endpoint 0x81 IN (Bulk, 64 bytes) — device-to-host data pipe
        └── MS Endpoint descriptor (links to MIDI jack)
```

**Why two interfaces?** The USB Audio Class specification requires an Audio Control interface even for pure MIDI devices. The actual MIDI data flows through the MIDI Streaming interface.

### 6.3 String Descriptors

Human-readable names shown in the OS and DAW software:

```c
char const* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  // 0: Language ID = English (US)
    "STMicroelectronics",            // 1: Manufacturer name
    "STM32 USB MIDI",                // 2: Product name (shown in device manager)
    "123456",                        // 3: Serial number
    "TinyUSB MIDI",                  // 4: MIDI interface name
};
```

The `tud_descriptor_string_cb()` callback converts these ASCII strings to UTF-16 format as required by the USB specification before sending them to the host.

---

## Step 7: USB Enumeration (What Happens When You Plug In)

When the USB cable is connected, the following exchange happens automatically:

```
HOST (PC)                              DEVICE (STM32)
    │                                      │
    │──── GET DEVICE DESCRIPTOR ──────────>│
    │<─── Device descriptor (18 bytes) ────│  tud_descriptor_device_cb()
    │                                      │
    │──── SET ADDRESS (e.g. addr=5) ──────>│
    │<─── ACK ─────────────────────────────│  (TinyUSB handles internally)
    │                                      │
    │──── GET CONFIGURATION DESCRIPTOR ───>│
    │<─── Config + Interface + Endpoint ───│  tud_descriptor_configuration_cb()
    │     descriptors (all at once)        │
    │                                      │
    │──── GET STRING DESCRIPTOR (idx=1) ──>│
    │<─── "STMicroelectronics" ────────────│  tud_descriptor_string_cb()
    │                                      │
    │──── GET STRING DESCRIPTOR (idx=2) ──>│
    │<─── "STM32 USB MIDI" ───────────────│  tud_descriptor_string_cb()
    │                                      │
    │──── SET CONFIGURATION (1) ──────────>│
    │<─── ACK ─────────────────────────────│  Device is now fully configured
    │                                      │
    │     DEVICE IS NOW READY FOR MIDI     │
    │         DATA TRANSFER                │
```

All of this is handled by TinyUSB's internal state machine. The three `tud_descriptor_*_cb()` callback functions in `usb_descriptors.c` are the only parts the application provides.

---

## Step 8: The USB Interrupt Handler

**File:** `Core/Src/stm32h5xx_it.c` — `USB_DRD_FS_IRQHandler()` (line 219)

```c
void USB_DRD_FS_IRQHandler(void)
{
    tud_int_handler(0);  // Delegate everything to TinyUSB
}
```

Every USB event triggers this hardware interrupt:
- **Bus reset** — host resets the device
- **Setup packet** — host sends a control request (descriptor requests, etc.)
- **Data transfer complete** — an endpoint finished sending or receiving data
- **Suspend/Resume** — power management events

`tud_int_handler(0)` tells TinyUSB to check the USB hardware registers and handle whatever event occurred. The `0` is the root hub port number (this MCU only has one USB port).

**This runs at priority 0 (highest)**, so USB events are never delayed by other interrupts. This is important because USB has strict timing requirements — the host expects responses within a few milliseconds.

---

## Step 9: The Main Loop

**File:** `Core/Src/main.c` — `main()` (lines 120-131)

```c
while (1)
{
    tud_task();     // Process any pending USB events
    midi_task();    // Run the MIDI application logic
}
```

This is a **bare-metal polling loop** (no RTOS). It runs as fast as the CPU allows (millions of iterations per second at 32 MHz).

### `tud_task()`
- Checks if the USB interrupt handler flagged any events.
- Processes completed transfers (moves data from hardware buffers to software FIFOs).
- Handles any pending control requests.
- Must be called frequently to keep USB communication responsive.

### `midi_task()`
- Contains the actual MIDI application logic (see Step 10).

---

## Step 10: The MIDI Application Logic

**File:** `Core/Src/main.c` — `midi_task()` (line 265)

This is the core application behavior. It does two things:

### 10.1 Drain Incoming MIDI Data

```c
uint8_t packet[4];
while (tud_midi_available()) tud_midi_packet_read(packet);
```

USB MIDI always creates both IN and OUT endpoints (bidirectional). Even if the application doesn't use incoming MIDI, the data must be read and discarded. Otherwise, the host's output buffer fills up and the host application (DAW) will block/freeze when trying to send MIDI to this device.

### 10.2 Send Note On / Note Off Every Second

```c
static uint32_t start_ms = 0;
static bool note_on = false;

if (HAL_GetTick() - start_ms > 1000)    // Every 1000 ms
{
    start_ms = HAL_GetTick();
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);  // Blink the on-board LED

    uint8_t cable_num = 0;   // MIDI cable/jack number (0 = first/only jack)
    uint8_t channel = 0;     // MIDI channel 1 (channels are 0-indexed in code)
    uint8_t note = 60;       // Note number 60 = Middle C (C4)
    uint8_t velocity = 100;  // How hard the note is played (0-127)

    if (note_on)
    {
        // Send Note Off: status byte 0x80, note 60, velocity 0
        uint8_t note_off[3] = { 0x80 | channel, note, 0 };
        tud_midi_stream_write(cable_num, note_off, 3);
    }
    else
    {
        // Send Note On: status byte 0x90, note 60, velocity 100
        uint8_t note_on_msg[3] = { 0x90 | channel, note, velocity };
        tud_midi_stream_write(cable_num, note_on_msg, 3);
    }
    note_on = !note_on;  // Toggle for next iteration
}
```

**The timeline looks like this:**
```
Time 0s:    Note On  (C4, velocity 100) + LED ON
Time 1s:    Note Off (C4, velocity 0)   + LED OFF
Time 2s:    Note On  (C4, velocity 100) + LED ON
Time 3s:    Note Off (C4, velocity 0)   + LED OFF
... repeats forever
```

---

## Step 11: How MIDI Messages Become USB Packets

When `tud_midi_stream_write()` is called with a 3-byte MIDI message, TinyUSB converts it to a **4-byte USB MIDI Event Packet** as defined by the USB MIDI specification:

```
Standard MIDI message (3 bytes):
┌──────────────┬──────────┬──────────┐
│ Status Byte  │ Data 1   │ Data 2   │
│ 0x90         │ 0x3C     │ 0x64     │
│ (Note On Ch1)│ (Note 60)│ (Vel 100)│
└──────────────┴──────────┴──────────┘

USB MIDI Event Packet (4 bytes):
┌──────────────────────────┬──────────────┬──────────┬──────────┐
│ Byte 0                   │ Byte 1       │ Byte 2   │ Byte 3   │
│ Cable Num | Code Index   │ Status Byte  │ Data 1   │ Data 2   │
│ 0x09                     │ 0x90         │ 0x3C     │ 0x64     │
│ (Cable 0, CIN=Note On)  │ (Note On Ch1)│ (Note 60)│ (Vel 100)│
└──────────────────────────┴──────────────┴──────────┴──────────┘
```

**Byte 0 breakdown:**
- **Upper nibble (bits 7-4):** Cable Number — identifies which virtual MIDI cable this packet belongs to. This device has one cable, so it's always `0`.
- **Lower nibble (bits 3-0):** Code Index Number (CIN) — tells the host what kind of MIDI message this is, so it knows how many of the following bytes are valid.

**Common CIN values:**
| CIN | MIDI Event                |
|-----|---------------------------|
| 0x8 | Note Off                  |
| 0x9 | Note On                   |
| 0xB | Control Change (CC)       |
| 0xC | Program Change            |
| 0xE | Pitch Bend                |

---

## Step 12: The TinyUSB Port Layer

**File:** `Core/Src/tusb_port.c`

This tiny file bridges TinyUSB's platform abstraction to the STM32 HAL:

```c
void tusb_hal_init(void) {
    // Empty — USB hardware was already initialized by HAL_PCD_Init()
}

uint32_t tusb_time_millis_api(void) {
    return HAL_GetTick();  // Returns milliseconds since boot (from SysTick)
}
```

TinyUSB needs a millisecond timer for:
- Timeout detection during USB transfers
- Periodic tasks (e.g., checking for bus idle)
- The timing in `midi_task()` (via `HAL_GetTick()`)

The `SysTick_Handler` in `stm32h5xx_it.c` increments this counter every 1 ms.

---

## Step 13: Complete Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                    SENDING MIDI (Device → Host)                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  1. midi_task() creates a 3-byte MIDI message                      │
│     { 0x90, 0x3C, 0x64 }   (Note On, Middle C, Velocity 100)      │
│                          │                                          │
│                          ▼                                          │
│  2. tud_midi_stream_write(cable=0, msg, 3)                         │
│     TinyUSB wraps it into a 4-byte USB MIDI packet:                │
│     { 0x09, 0x90, 0x3C, 0x64 }                                    │
│                          │                                          │
│                          ▼                                          │
│  3. Packet is placed in the TX FIFO buffer (64 bytes)              │
│                          │                                          │
│                          ▼                                          │
│  4. TinyUSB submits the buffer to IN Endpoint 0x81                 │
│     The USB hardware (DCD) loads it into the endpoint buffer       │
│                          │                                          │
│                          ▼                                          │
│  5. Host polls the IN endpoint (USB bulk transfer)                 │
│     Data is sent over the USB cable                                │
│                          │                                          │
│                          ▼                                          │
│  6. Host OS MIDI driver receives the packet                        │
│     - Windows: USB Audio Class driver → MIDI API                   │
│     - macOS:   CoreMIDI                                            │
│     - Linux:   ALSA sequencer                                      │
│                          │                                          │
│                          ▼                                          │
│  7. DAW / MIDI software receives Note On event                     │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                   RECEIVING MIDI (Host → Device)                    │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  1. Host application sends a MIDI message                          │
│                          │                                          │
│                          ▼                                          │
│  2. Host OS writes 4-byte USB MIDI packet to OUT Endpoint 0x01    │
│                          │                                          │
│                          ▼                                          │
│  3. USB_DRD_FS_IRQHandler() fires → tud_int_handler(0)            │
│     TinyUSB moves data from endpoint hardware to RX FIFO          │
│                          │                                          │
│                          ▼                                          │
│  4. midi_task() calls tud_midi_available() → returns > 0           │
│     tud_midi_packet_read(packet) retrieves the 4-byte packet      │
│                          │                                          │
│                          ▼                                          │
│  5. In this demo: packet is discarded (read but not used)          │
│     A real application would parse and act on the MIDI data        │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Step 14: Boot Sequence Summary

Here is the complete sequence from power-on to MIDI operation:

| # | Function | File | What Happens |
|---|----------|------|--------------|
| 1 | Reset vector → `main()` | `main.c:70` | CPU starts executing |
| 2 | `HAL_Init()` | `main.c:80` | Configure SysTick (1 ms tick), NVIC, Flash prefetch |
| 3 | `SystemClock_Config()` | `main.c:139` | Enable HSI (32 MHz CPU) and HSI48 (48 MHz for USB) |
| 4 | `MX_GPIO_Init()` | `main.c:226` | Configure PA5 as output (LED), PC4 as input |
| 5 | `MX_USB_PCD_Init()` | `main.c:190` | Configure USB peripheral (Full-Speed, embedded PHY) |
| 5a | → `HAL_PCD_MspInit()` | `hal_msp.c:83` | Route 48 MHz to USB, enable power, enable IRQ |
| 6 | `tusb_init()` | `main.c:99` | Initialize TinyUSB stack, connect to bus |
| 7 | (Host detects device) | — | Enumeration begins (descriptor exchange) |
| 8 | `tud_descriptor_device_cb()` | `usb_descriptors.c:42` | Host reads device identity |
| 9 | `tud_descriptor_configuration_cb()` | `usb_descriptors.c:75` | Host reads interfaces/endpoints |
| 10 | `tud_descriptor_string_cb()` | `usb_descriptors.c:99` | Host reads human-readable names |
| 11 | (Host sets configuration) | — | Device is now "configured" — MIDI endpoints are active |
| 12 | Main loop: `tud_task()` + `midi_task()` | `main.c:120` | Continuous USB processing + MIDI note sending |

---

## Key Concepts Glossary

| Term | Meaning |
|------|---------|
| **PCD** | Peripheral Controller Driver — STM32 HAL's name for the USB device hardware abstraction |
| **DCD** | Device Controller Driver — TinyUSB's name for the same hardware layer |
| **TUD** | TinyUSB Device — prefix for all device-mode API functions |
| **Endpoint** | A data channel in USB. EP0 = control (setup/config), EP1 OUT/IN = bulk data |
| **Bulk Transfer** | USB transfer type used for MIDI — reliable, no guaranteed timing, uses spare bandwidth |
| **Descriptor** | A data structure that tells the host what the device is and how to communicate with it |
| **Enumeration** | The process where the host identifies a newly connected device by reading its descriptors |
| **CIN** | Code Index Number — the 4-bit identifier in byte 0 of every USB MIDI packet |
| **Cable Number** | Virtual MIDI cable identifier — allows multiple MIDI ports over one USB connection |
| **MIDI Channel** | Logical channel (1-16) within a MIDI stream — encoded in the lower nibble of the status byte |
| **MSP** | MCU Support Package — STM32 HAL functions for low-level peripheral setup (clocks, GPIO, IRQ) |
