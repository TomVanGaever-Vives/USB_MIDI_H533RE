# TinyUSB MIDI Integration for STM32H533RE

This project has been configured to use tinyUSB as a USB MIDI device on the NUCLEO-H533RE board.

## What Was Added

### 1. TinyUSB Library
- Located in `Middlewares/tinyusb/`
- Cloned from the official tinyUSB repository

### 2. Configuration Files
- **Core/Inc/tusb_config.h**: TinyUSB configuration
  - Enabled MIDI device class
  - Configured for STM32H5 MCU
  - Set buffer sizes and endpoints

### 3. USB Descriptors
- **Core/Src/usb_descriptors.c**: USB device descriptors
  - Vendor ID: 0xCAFE (change to your own)
  - Product ID: 0x4001
  - Device appears as "STM32 USB MIDI"

### 4. Port Layer
- **Core/Src/tusb_port.c**: Connects tinyUSB to STM32 HAL
  - Bridges HAL PCD callbacks to tinyUSB interrupt handler
  - Initializes USB peripheral

### 5. Application Code
- **Core/Src/main.c**: Updated with:
  - TinyUSB initialization
  - Main loop calling `tud_task()` for USB processing
  - Example MIDI task that sends Note On/Off messages every second
  - Toggles LED (PA5) with MIDI messages

### 6. Keil Project
- **MDK-ARM/USB_MIDI2.uvprojx**: Updated with:
  - TinyUSB include paths
  - TinyUSB source files added to build
  - USB descriptor and port layer files

## How It Works

1. On startup, the STM32 initializes the USB peripheral via HAL
2. TinyUSB stack is initialized with `tusb_init()`
3. USB peripheral is started with `tusb_hal_init()`
4. In the main loop:
   - `tud_task()` processes USB events
   - `midi_task()` handles MIDI application logic

## Example MIDI Task

The current implementation sends a MIDI Note On (Middle C) every second, followed by Note Off after another second. The LED on PA5 toggles with each message.

### Modifying MIDI Behavior

To send custom MIDI messages, modify the `midi_task()` function in [main.c](../Core/Src/main.c):

```c
// Send a Note On message
uint8_t cable_num = 0;
uint8_t channel = 0;  // Channel 1
uint8_t note = 60;    // Middle C
uint8_t velocity = 100;

uint8_t note_on[3] = { 0x90 | channel, note, velocity };
tud_midi_stream_write(cable_num, note_on, 3);
```

### Reading MIDI Input

To read incoming MIDI messages:

```c
uint8_t packet[4];
while (tud_midi_available()) {
    if (tud_midi_packet_read(packet)) {
        // Process MIDI packet
        // packet[0] = Cable Number and Code Index Number
        // packet[1-3] = MIDI message bytes
    }
}
```

## Building and Running

1. Open the project in Keil MDK-ARM
2. Build the project (F7)
3. Flash to the NUCLEO-H533RE board
4. Connect the USB connector to your PC
5. The device should enumerate as "STM32 USB MIDI"
6. Use a MIDI monitoring tool to see the messages

## Testing

### On Windows
- Use MIDI-OX or similar MIDI monitor software
- The device should appear in the MIDI input devices list

### On macOS
- Use Audio MIDI Setup
- Or use a DAW like Ableton Live, Logic Pro, etc.

### On Linux
- Use `aseqdump -l` to list MIDI devices
- Use `aseqdump -p [port]` to monitor MIDI messages

## Important Notes

1. **Vendor/Product ID**: Change the VID/PID in [usb_descriptors.c](../Core/Src/usb_descriptors.c) if distributing publicly
2. **STM32CubeMX**: If regenerating code with CubeMX, make sure to preserve the USER CODE sections in main.c
3. **USB Port**: Use the USB connector on the board (not the ST-LINK USB)
4. **Clock Configuration**: HSI48 is enabled for USB (48 MHz required)

## Troubleshooting

### Device not recognized
- Check USB cable connection
- Verify HSI48 clock is enabled in clock configuration
- Check Windows Device Manager for unknown devices

### MIDI messages not received
- Verify MIDI monitoring software is listening to correct port
- Check that `tud_task()` is being called in main loop
- Use a debugger to verify `tud_mounted()` returns true

### Build errors
- Ensure all tinyUSB files are added to Keil project
- Check include paths are correct
- Verify tusb_config.h is in Core/Inc

## Resources

- [TinyUSB Documentation](https://docs.tinyusb.org/)
- [TinyUSB GitHub](https://github.com/hathach/tinyusb)
- [USB MIDI Specification](https://www.usb.org/sites/default/files/midi10.pdf)
