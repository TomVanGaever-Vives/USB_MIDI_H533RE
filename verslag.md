# USB MIDI 4x4 Knoppenmatrix
## Technisch Verslag

---

## 1. Inleiding

Dit verslag beschrijft de implementatie van een USB MIDI-keyboard met een 4x4 knoppenmatrix aangestuurd via een MCP23S17 I/O expander. Het project demonstreert:

- USB MIDI Device Class communicatie
- SPI-communicatie met een I/O expander  
- Matrix scanning met debouncing
- Hardware abstractie op een STM32H5 microcontroller
- TinyUSB middleware integratie

Dit document biedt een stap-voor-stap gids om het project zelf op te zetten en uit te voeren.

---

## 2. Projectoverzicht

### 2.1 Doelstelling

Bouwen van een USB MIDI-keyboard dat via USB aan een computer kan worden aangesloten en MIDI-noten genereert wanneer knoppen van de 4x4 matrix worden ingedrukt.

### 2.2 Systeemarchitectuur

```
┌─────────────────────┐
│  4x4 Knoppenmatrix  │  16 mechanische toetsen
│  (16 knoppen)       │  in rijen en kolommen
└──────────┬──────────┘
           │ (rijen & kolommen)
           │
      ┌────v────────────────┐
      │  MCP23S17           │  SPI I/O expander
      │  (I/O Expander)     │  • GPIOA: kolom-outputs
      │                     │  • GPIOB: rij-inputs
      └────────┬────────────┘
               │ (SPI: SCK, MOSI, MISO, CS)
               │
      ┌────────v──────────────────┐
      │   STM32H533 (Nucleo)      │  Microcontroller
      │   • SPI Master            │  • Matrix scanning
      │   • USB Device Mode       │  • Debouncing
      │   • TinyUSB Stack         │  • MIDI generatie
      └────────┬──────────────────┘
               │ (USB)
               │
      ┌────────v──────────────┐
      │  Computer/DAW         │  MIDI ontvanger
      │  (MIDI Synthesizer)   │
      └───────────────────────┘
```

---

## 3. Hardware Setup

### 3.1 Benodigde Componenten

| Component | Beschrijving | Rol |
|-----------|-------------|-----|
| **STM32H533RE Nucleo Board** | ARM Cortex-M33 microcontroller | Centrale verwerking |
| **MCP23S17** | 16-bit SPI I/O Expander | Matrix-besturing |
| **4x4 Knoppenmatrix** | 16 push buttons in matrix | Gebruikersinput |
| **USB-kabel (Micro-B)** | Voor voeding en MIDI-communicatie | USB-verbinding |
| **Jumperkabels (Male-Female)** | Voor breadboard-verbinding | Prototyping |

### 3.2 Pinout en Verbindingen

#### STM32H533 ↔ MCP23S17 (SPI)

| STM32 Pin | MCP23S17 Pin | Functie |
|-----------|-------------|---------|
| **PA5** | **CS** | Chip Select (actief laag) |
| **PB13** | **CLK** | SPI Clock |
| **PB14** | **MISO** | Serial Data In (van MCP) |
| **PB15** | **MOSI** | Serial Data Out (naar MCP) |
| **GND** | **GND** | Mass (aarding) |
| **+3.3V** | **VDD** | Voeding |

#### MCP23S17 GPIO Mapping

**GPIOA** (Kolommen - **Outputs**)
```
GPA0 = Kolom 0 (laag = geselecteerd)
GPA1 = Kolom 1
GPA2 = Kolom 2
GPA3 = Kolom 3
```

**GPIOB** (Rijen - **Inputs met Pull-ups**)
```
GPB0 = Rij 0 (laag = knop ingedrukt)
GPB1 = Rij 1
GPB2 = Rij 2
GPB3 = Rij 3
```

### 3.3 USB-voeding Jumper-instellingen

De **Nucleo-H533RE** heeft jumpers voor USB-voeding configuratie:

| Jumper | Instelling | Beschrijving |
|--------|-----------|--------------|
| **P1** | **Via USB** | USB levert voeding (normaal geval) |
| **P2** | **Externe voeding** | 5V externe bron (optioneel) |

**Configuratie:** 
- Voor dit project: plaats jumper op **P1** zodat voeding via USB komt
- Dit is standaard ingesteld op de meeste Nucleo boards

**Verificatie:**
- LED1 (groen) op STM32 board brandt → voeding OK
- Geen voeding nodig via externe adapter

---

## 4. USB Configuratie

### 4.1 Wat is USB MIDI Device Class?

USB MIDI Class is een USB-standaard die muziekinstrumenten toestaat communication via USB zonder custom drivers:

- **Eenvoudige plug-and-play** op Windows, macOS, Linux
- **MIDI-berichten** over USB-bulktransfer
- **16 MIDI-kanalen** mogelijk per USB-aansluiting
- Compatibel met alle DAW-software (FL Studio, Ableton, Logic, etc.)

### 4.2 USB Descriptor

Een USB descriptor definieert wat het apparaat is voor het host-systeem.

**Minimale MIDI USB Descriptor structuur:**

```
Device Descriptor
├─ idVendor: 0x2E8A (TinyUSB)
├─ idProduct: 0x000B (MIDI device)
├─ bDeviceClass: 0x00 (Defined at interface level)
└─ bConfigurationValue: 1

Configuration Descriptor
└─ Interface Descriptor
   ├─ bInterfaceClass: 0x01 (Audio)
   ├─ bInterfaceSubClass: 0x03 (MIDI Streaming)
   └─ Endpoints:
      ├─ EP OUT (Host → Device): MIDI In
      ├─ EP IN (Device → Host): MIDI Out
```

**Praktische betekenis:**
- Windows/macOS herkent dit apparaat automatisch als "USB MIDI Device"
- Geen driverinstallatie nodig
- Apparaat verschijnt in alle DAW's onder "MIDI In/Out"

### 4.3 Configuratie in het Project

In `tusb_config.h`:

```c
#define CFG_TUSB_MCU           OPT_MCU_STM32H5
#define CFG_TUSB_OS            OPT_OS_NONE
#define CFG_TUSB_RHPORT0_MODE  OPT_MODE_DEVICE
#define CFG_TUD_MIDI           1
```

Dit vertelt TinyUSB: "Configureer dit board als MIDI USB device"

---

## 5. TinyUSB Library

### 5.1 Wat is TinyUSB?

TinyUSB is een opensource USB stack speciaal voor embedded systems:

- **Lichtgewicht** (klein RAM/Flash footprint)
- **Meerdere USB device classes** (MIDI, HID, CDC, etc.)
- **Cross-platform** (STM32, nRF52, RP2040, etc.)
- **Gratis and open-source** (MIT licentie)

### 5.2 Waarom TinyUSB voor dit project?

| Voordeel | Reden |
|----------|--------|
| **MIDI class ingebouwd** | Geen eigen MIDI stack programmeren |
| **Abstractie van USB hardware** | Werkt automatisch met STM32 USB peripheral |
| **Non-blocking I/O** | USB communiceert in achtergrond |
| **Documentatie** | Veel voorbeelden beschikbaar |

### 5.3 TinyUSB in dit Project

Lokatie: `Middlewares/tinyusb/`

**Sleutelbestanden:**
- `src/tusb.h` - Hoofd API header
- `src/common/tusb_common.h` - Gemeenschappelijke types
- `drivers/usb/...` - USB hardware drivers

**Initialisatie (main.c):**

```c
// TinyUSB initialiseren
tusb_rhport_init_t dev_init = {
  .role  = TUSB_ROLE_DEVICE,
  .speed = TUSB_SPEED_AUTO
};
tusb_init(BOARD_TUD_RHPORT, &dev_init);
```

**USB servicing (main loop):**

```c
while (1) {
  tud_task();              // USB stack verwerken
  keypad_task();           // Matrix scannen
  midi_task();             // Inkomende MIDI verwerken
}
```

---

## 6. Software Architectuur

### 6.1 Functionele Lagen

```
┌────────────────────────────────────┐
│  Application Layer                 │
│  - Keypad_task()                   │
│  - MIDI_task()                     │
│  - Led_blinking_task()             │
└────────────────────────────────────┘
                 ↓
┌────────────────────────────────────┐
│  Matrix Scanning Layer             │
│  - Keypad_scan()                   │
│  - Debounce logic                  │
│  - State machines                  │
└────────────────────────────────────┘
                 ↓
┌────────────────────────────────────┐
│  I/O Expander Driver (MCP23S17)    │
│  - mcp23s17_read_reg()             │
│  - mcp23s17_write_reg()            │
│  - Bit-bang SPI                    │
└────────────────────────────────────┘
                 ↓
┌────────────────────────────────────┐
│  HAL / USB Stack                   │
│  - GPIO / SPI hardware             │
│  - TinyUSB middleware              │
│  - USB DRD peripheral              │
└────────────────────────────────────┘
```

### 6.2 Dataflow: Knopdruk → MIDI

```
1. Gebruiker drukt knop in
   ↓
2. Knop vervang naar laag (contact maken)
   ↓
3. keypad_scan() - Matrix scanning via SPI
   - Schrijf kolom laag naar MCP GPIOA
   - Lees rijen van MCP GPIOB
   - Controleer welke rij ook laag is
   ↓
4. Debounce - 5 identieke scans nodig (100ms stabiliteit)
   ↓
5. Toestandsverandering detecteren (1→0 = ingedrukt)
   ↓
6. MIDI Note On bericht bouwen
   - Status byte: 0x90 (Note On channel 0)
   - Data byte 1: Nootnummer (60-86)
   - Data byte 2: Velocity (127 - maximum)
   ↓
7. tud_midi_packet_write() - Via USB naar computer
   ↓
8. DAW/Synthesizer ontvangt MIDI-bericht
   - Speelt sound af
```

---

## 7. Sleutelcomponenten Code Analysis

### 7.1 MCP23S17 Initialisatie

**Wat doet dit?**
Configureert GPIOA als outputs (kolommen aansturen) en GPIOB als inputs met pull-ups (rijen lezen).

```c
static void mcp23s17_init(void)
{
  // GPIOA = kolommen (outputs)
  mcp23s17_write_reg(MCP_IODIRA, 0x00);    // 0=output, 1=input
  mcp23s17_write_reg(MCP_GPPUA,  0x00);    // Geen pull-ups op outputs

  // GPIOB = rijen (inputs met pull-ups)
  mcp23s17_write_reg(MCP_IODIRB, 0x0F);    // 0x0F = alle 4 rijen als inputs
  mcp23s17_write_reg(MCP_GPPUB,  0x0F);    // 0x0F = pull-ups ingeschakeld
  
  // Alle kolommen initieel hoog (inactief)
  mcp23s17_write_reg(MCP_REG_GPIOA, 0x0F);

  // Initialiseer state arrays
  for (int i = 0; i < 4; i++) {
    keypad_state[i]      = 0x0F;   // Alle knoppen los (hoog = niet ingedrukt)
    keypad_prev[i]       = 0x0F;   // Vorige toestand
    keypad_candidate[i]  = 0x0F;   // Debounce kandidaat
    keypad_stable_cnt[i] = 0;      // Debounce teller
  }
}
```

**Registers uitgelegd:**
- `IODIRA (0x00)` - I/O Direction Register A
  - Bit = 0 → Output (we sturen dit aan)
  - Bit = 1 → Input (we lezen dit)
  
- `GPPUA (0x0C)` - GPIO Pull-Up Resistor A
  - Bit = 0 → Geen internal pull-up
  - Bit = 1 → 100kΩ internal pull-up

---

### 7.2 SPI Bit-Bang Implementatie

**Wat doet dit?**
Stuurt en ontvangt 1 byte via GPIO zonder hardware SPI module. Handmatige klokgenering.

```c
static uint8_t spi_transfer_byte(uint8_t data)
{
  uint8_t received = 0;

  // MSB first (Most Significant Bit): bit 7 → bit 0
  for (int i = 7; i >= 0; i--)
  {
    // Zet MOSI bit (PB15)
    if (data & (1u << i))
      HAL_GPIO_WritePin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_PIN_SET);
    else
      HAL_GPIO_WritePin(SPI_MOSI_PORT, SPI_MOSI_PIN, GPIO_PIN_RESET);

    // SCK stijgende flank (PB13 omhoog)
    HAL_GPIO_WritePin(SPI_SCK_PORT, SPI_SCK_PIN, GPIO_PIN_SET);

    // Lees MISO bit (PB14) tijdens stijgende flank
    if (HAL_GPIO_ReadPin(SPI_MISO_PORT, SPI_MISO_PIN) == GPIO_PIN_SET)
      received |= (1u << i);  // Bit instellen

    // SCK dalende flank (PB13 omlaag)
    HAL_GPIO_WritePin(SPI_SCK_PORT, SPI_SCK_PIN, GPIO_PIN_RESET);
  }

  return received;  // Ontvangen byte teruggeven
}
```

**SPI Protocol:**
```
Tijdlijn voor 1 bit (bijv. bit 7):

MOSI:  [---D7---]
         (zet data)

SCK:   [____⎺⎺⎺_]
       stijging (read)

MISO:  [---R7---]
        (slave responds)
```

---

### 7.3 Matrix Scanning met Debouncing

**Wat doet dit?**
Scant de 4x4 matrix elke kolom, leest welke rijen hoog/laag zijn, en filtert ruis via debouncing.

```c
static void keypad_scan(void)
{
  // Loop door elke kolom (0-3)
  for (int row = 0; row < 4; row++)
  {
    // Drijf kolom 'row' omlaag, rest omhoog
    // Voorbeeld: row=0 → 0b1110 = 0xE → alleen kolom 0 laag
    uint8_t row_pattern = ~(1 << row) & 0x0F;
    mcp23s17_write_reg(MCP_REG_GPIOA, row_pattern);

    // Lees rijen twee keer (debounce verificatie)
    uint8_t read1 = mcp23s17_read_reg(MCP_REG_GPIOB) & 0x0F;
    uint8_t read2 = mcp23s17_read_reg(MCP_REG_GPIOB) & 0x0F;

    // Beide aflezingen identiek? (geen ruis)
    if (read1 != read2) {
      keypad_stable_cnt[row] = 0;  // Reset debounce teller
      continue;
    }

    // Dezelfde als kandidaat?
    if (read1 == keypad_candidate[row]) {
      // Teller verhogen totdat DEBOUNCE_COUNT (5) bereikt
      if (keypad_stable_cnt[row] < DEBOUNCE_COUNT)
        keypad_stable_cnt[row]++;
      
      // Na 5 identieke scans: accepteer als stabiele toestand
      if (keypad_stable_cnt[row] >= DEBOUNCE_COUNT)
        keypad_state[row] = read1;
    } else {
      // Nieuwe kandidaat waarde
      keypad_candidate[row]  = read1;
      keypad_stable_cnt[row] = 1;  // Restart teller
    }
  }

  // Zet alle kolommen terug hoog (inactief)
  mcp23s17_write_reg(MCP_REG_GPIOA, 0x0F);
}
```

**Debounce Logica:**
```
Tijd:        0ms   20ms  40ms  60ms  80ms  100ms  120ms
                   |     |     |     |     |      |
Raw:         1→0   0→1   0     0     0     0      0
Candidate:   X     1     0     0     0     0      0
Cnt:         0     1     1     2     3     4      5
State:       1     1     1     1     1     1→0    0

Acceptatie gebeurt na 5×20ms = 100ms stabiliteit
5 identieke waarden = vertrouwen dat het geen ruis is
```

---

### 7.4 MIDI Note On/Off Generatie

**Wat doet dit?**
Detecteert toestandsveranderingen en stuurt MIDI Note On/Off berichten.

```c
void keypad_task(void)
{
  // Scan slechts elke 20ms
  static uint32_t scan_ms = 0;
  if (board_millis() - scan_ms < 20)
    return;
  scan_ms = board_millis();

  // Scant matrix
  keypad_scan();

  // Alleen MIDI sturen als USB aangesloten is
  if (!tud_mounted())
    return;

  // Loop door alle 16 knoppen
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      // Extract bit voor deze knop
      uint8_t current_bit = (keypad_state[row] >> col) & 1;
      uint8_t prev_bit = (keypad_prev[row] >> col) & 1;

      // KNOP INGEDRUKT (1→0 transitie)
      // Vorig = los (1), huidige = ingedrukt (0)
      if (prev_bit && !current_bit && !active_notes[row][col])
      {
        uint8_t note = note_map[col][row];

        // MIDI Note On packet bouwen
        uint8_t note_on[4] = {
          0x09,              // CIN 9 = Note On
          0x90,              // Status: Note On channel 0
          note,              // Nootnummer (60-127)
          127                // Velocity (0-127, 127=max)
        };
        tud_midi_packet_write(note_on);
        active_notes[row][col] = note;  // Onthouden welke noot actief is
      }

      // KNOP LOSGELATEN (0→1 transitie)
      // Vorig = ingedrukt (0), huidige = los (1)
      else if (!prev_bit && current_bit && active_notes[row][col])
      {
        uint8_t note = active_notes[row][col];

        // MIDI Note Off packet bouwen
        uint8_t note_off[4] = {
          0x08,              // CIN 8 = Note Off
          0x80,              // Status: Note Off channel 0
          note,              // Dezelfde nootnummer
          0                  // Velocity (genegeerd voor Note Off)
        };
        tud_midi_packet_write(note_off);
        active_notes[row][col] = 0;  // Markeer als inactief
      }
    }
  }

  // Update vorige toestand voor volgende cyclus
  for (int i = 0; i < 4; i++)
    keypad_prev[i] = keypad_state[i];
}
```

**MIDI Packet Structuur:**

| Byte | Naam | Waarde | Betekenis |
|------|------|--------|-----------|
| 0 | CIN + Cable | 0x09 | Code Index Number 9 = Note On |
| 1 | Status | 0x90 | Note On op MIDI kanaal 0 (0x90 = 0x90 \| 0) |
| 2 | Data 1 | 60-127 | MIDI nootnummer |
| 3 | Data 2 | 0-127 | Velocity (aanslagnelheid) |

---

## 8. MIDI Note Mapping

### 8.1 Wiskundige Formule

De standaardformule voor matrix-naar-MIDI mapping is:

$$\text{midi\_noot} = \text{BASIS\_NOOT} + (\text{rij} \times 4) + \text{kolom}$$

Met **BASIS_NOOT = 60** (Midden C / C4):

#### Berekening:

**Rij 0:** (0 × 4) = +0
- Kolom 0: 60 + 0 + 0 = **60 (C4)**
- Kolom 1: 60 + 0 + 1 = **61 (C#4)**
- Kolom 2: 60 + 0 + 2 = **62 (D4)**
- Kolom 3: 60 + 0 + 3 = **63 (D#4)**

**Rij 1:** (1 × 4) = +4
- Kolom 0: 60 + 4 + 0 = **64 (E4)**
- Kolom 1: 60 + 4 + 1 = **65 (F4)**
- Kolom 2: 60 + 4 + 2 = **66 (F#4)**
- Kolom 3: 60 + 4 + 3 = **67 (G4)**

**Rij 2:** (2 × 4) = +8
- Kolom 0: 60 + 8 + 0 = **68 (G#4)**
- Kolom 1: 60 + 8 + 1 = **69 (A4)**
- Kolom 2: 60 + 8 + 2 = **70 (A#4)**
- Kolom 3: 60 + 8 + 3 = **71 (B4)**

**Rij 3:** (3 × 4) = +12
- Kolom 0: 60 + 12 + 0 = **72 (C5)**
- Kolom 1: 60 + 12 + 1 = **73 (C#5)**
- Kolom 2: 60 + 12 + 2 = **74 (D5)**
- Kolom 3: 60 + 12 + 3 = **75 (D#5)**

**Volledige Matrix:**

```
      KOL0  KOL1  KOL2  KOL3
RIJ0   60    61    62    63   (C4 - D#4)
RIJ1   64    65    66    67   (E4 - G4)
RIJ2   68    69    70    71   (G#4 - B4)
RIJ3   72    73    74    75   (C5 - D#5)
```

### 8.2 Implementatie in Code

In `main.c` is een aangepaste mapping gebruikt (chromatische noten, niet strikt volgens formule):

```c
const uint8_t note_map[4][4] = {
  {60, 62, 64, 65},  // Rij 0: C4, D4, E4, F4
  {67, 69, 71, 72},  // Rij 1: G4, A4, B4, C5
  {74, 76, 77, 79},  // Rij 2: D5, E5, F5, G5
  {81, 83, 84, 86}   // Rij 3: A5, B5, C6, D6
};
```

Deze gebruiksvriendelijkere indeling springt over semitonen waar nodig (bijv. geen F#, B#, E#), wat beter speelt voor muziekpatronen.

---

## 9. Installatie & Configuratie

### 9.1 Stap 1: Project Setup

```
1. Download STM32CubeMX GUI
2. Open USB_MIDI2.ioc bestand
3. Verifieer pinout:
   - PA5  = GPIO Output (MCP CS)
   - PB13 = GPIO Output (SPI SCK)
   - PB14 = GPIO Input (SPI MISO)
   - PB15 = GPIO Output (SPI MOSI)
4. Generate code
```

### 9.2 Stap 2: TinyUSB Configuratie

**Locatie:** `Core/Inc/tusb_config.h`

```c
// MCU selectie
#define CFG_TUSB_MCU OPT_MCU_STM32H5

// OS configuratie
#define CFG_TUSB_OS OPT_OS_NONE

// USB mode: Device
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE

// Enable MIDI class
#define CFG_TUD_MIDI 1

// MIDI streaming endpoints
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64
```

### 9.3 Stap 3: Insluitbestanden

**Zorg dat tusb_port.c** definiëert:

```c
// TinyUSB HAL callback - USB interrupt handler
void USB_DRD_FS_IRQHandler(void)
{
  tud_int_handler(BOARD_TUD_RHPORT);
}

// TinyUSB Time API
uint32_t tusb_time_millis_api(void)
{
  return HAL_GetTick();
}
```

### 9.4 Stap 4: Compilatie

```bash
# In Keil µVision
1. Project → Build (of Ctrl+F7)
2. Check voor linker errors
3. Project → Flash → Download (of Ctrl+F8)
   - Verbind STM32 via USB
   - Download firmware naar microcontroller
```

---

## 10. Testing & Troubleshooting

### 10.1 Hardware Testing

**USB Voeding Verificatie:**
```
1. Plug STM32 in via USB
2. LED1 (groen) moet oplichten
   - Oké: USB voeding werkt
   - Niet OK: Controleer USB kabel, voeding probleem
```

**Matrix Testing:**
```
1. Druk een knop in de matrix in
2. Controleer met oscilloscoop/multimeter:
   - Kolom-output (GPA) moet laag gaande
   - Rij-input (GPB) moet laag reageren
   - Beide laag = knop detectie
```

### 10.2 Software Testing

**MIDI Monitoring (Windows):**
```
1. Download MIDI-OX (freeware MIDI monitor)
2. Plug STM32 in via USB
3. MIDI-OX → Options → MIDI Devices
4. Selecteer "USB MIDI Device" als Input
5. Druk knoppen in keyboard
6. Controleer MIDI berichten verschijnen in MIDI-OX
```

**MIDI Monitoring (macOS):**
```
1. Download SimpleSynth of andere DAW
2. Apparaat verschijnt automatisch in MIDI-instellingen
3. Synthesizer speelt noten af bij indrukken
```

### 10.3 Veelgestelde Problemen

| Probleem | Oorzaak | Oplossing |
|----------|---------|-----------|
| "Onherkenend USB apparaat" | USB descriptor fout | Check `CFG_TUD_MIDI` in tusb_config.h |
| Geen MIDI berichten in DAW | Device niet gemount | `tud_mounted()` controle, USB interrupt enable |
| Flikkering/sporadische input | Debouncing ook streng | Verhoog `DEBOUNCE_COUNT` naar 10 |
| SPI communicatie faalt | CS pin verkeerd | Controleer PA5 = GPIO Output, initieel HIGH |
| Knop reageert niet | Matrix scan fout | GPIOA/GPIOB configuratie in MCP23S17 controleren |

---

## 11. Systeeminitialisatie Volgorde

**Waarom de volgorde belangrijk is:**

```c
int main(void) {
  // 1. HAL initialiseren (STM32 basis)
  HAL_Init();
  SystemClock_Config();
  
  // 2. GPIO's instellen (inclusief SPI pins)
  MX_GPIO_Init();
  MX_SPI_BitBang_Init();
  
  // 3. USB hardware initialiseren
  MX_USB_Init();
  
  // 4. MCP23S17 configureren (nu SPI beschikbaar is)
  mcp23s17_init();
  
  // 5. TinyUSB stack starten
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  
  // 6. Main loop - USB servicing is prioriteit
  while (1) {
    tud_task();        // Eerste! USB moet responsive zijn
    keypad_task();     // Daarna: matrix scannen
    midi_task();       // Laast: inkomende MIDI verwerken
  }
}
```

**Waarom deze volgorde:**
1. HAL = Hardware driver basis
2. GPIO/SPI = Communicatie kanalen beschikbaar
3. USB hardware = Physical interface gereed
4. MCP23S17 = Nu kunnen we deze configureren via SPI
5. TinyUSB = Middleware klaar om te werken
6. Main loop = USB moet snelste refresh krijgen

---

## 12. Conclusie

Dit project demonstreert een volledig werkende **USB MIDI keyboard** implementatie met:

✅ **Hardware Architectuur:**
- STM32H533 als centrale controller
- MCP23S17 SPI I/O expander voor matrix-uitbreiding
- 4x4 knoppenmatrix voor 16 noten

✅ **Software Architectuur:**
- Gelaagd ontwerp: applicatie → driver → HAL
- Debouncing voor betrouwbare invoer
- TinyUSB middleware voor USB MIDI

✅ **USB MIDI Integratie:**
- USB Device Class communicatie
- Automatic plug-and-play op alle platforms
- Echte MIDI Note On/Off berichten

✅ **Leerwaarde:**
- Embedded systems concept
- Microcontroller communicatieprotocollen (SPI, USB)
- Matrix scanning en debouncing theorie
- Middleware integratie

Dit project kan uitgebreid worden met:
- Velocity sensors (aanslagnelheid)
- Polyphonic aftertouch
- Control Change (CC) sliders
- USB MIDI host mode (inkomende MIDI verwerken)
- Wireless via Bluetooth

---

## Bijlage A: Gebruikte Registers MCP23S17

| Register | Adres | Functie |
|----------|-------|---------|
| IODIRA | 0x00 | I/O Direction PORTA |
| IODIRB | 0x01 | I/O Direction PORTB |
| IPOLA | 0x02 | Input Polarity PORTA |
| IPOLB | 0x03 | Input Polarity PORTB |
| GPINTENA | 0x04 | Interrupt Enable PORTA |
| GPINTENB | 0x05 | Interrupt Enable PORTB |
| GPPUA | 0x0C | Pull-Up PORTA |
| GPPUB | 0x0D | Pull-Up PORTB |
| GPIOA | 0x12 | GPIO Value PORTA (Lezen/Schrijven) |
| GPIOB | 0x13 | GPIO Value PORTB (Lezen/Schrijven) |
| OLATA | 0x14 | Output Latch PORTA |
| OLATB | 0x15 | Output Latch PORTB |

---

## Bijlage B: MIDI Status Bytes

| Bericht | Status Byte | Data B1 | Data B2 |
|---------|------------|---------|---------|
| Note Off | 0x8n | Nootnummer | Velocity |
| Note On | 0x9n | Nootnummer | Velocity |
| Poly Pressure | 0xAn | Nootnummer | Druk |
| Control Change | 0xBn | CC nummer | CC waarde |
| Program Change | 0xCn | Program | — |
| Channel Pressure | 0xDn | Druk | — |
| Pitch Bend | 0xEn | LSB | MSB |

_n = MIDI kanaal (0-15, weergegeven als 1-16 in DAW's)_

---

**Afsluiting:** Dit verslag is een complete documentatie en gids voor het USB MIDI matrix keyboard project. Alle hardware-instellingen, software-lagen, en implementatiedetails zijn uitgelegd om zelf dit project opnieuw op te kunnen zetten en te begrijpen.