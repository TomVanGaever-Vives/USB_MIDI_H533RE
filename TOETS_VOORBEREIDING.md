# USB MIDI Keyboard - TOETS VOORBEREIDING
## Alles wat je moet kennen voor morgen

---

## 1. PROJECT ARCHITECTUUR

### 1.1 Hardware Lagen (De Keten)

```
KNOP INGEDRUKT
        ↓
    [4x4 MATRIX]
  16 knoppen in
  rijen + kolommen
        ↓
  [MCP23S17]
  SPI I/O Expander
  - Leest rijen (GPIOB)
  - Stuurt kolommen (GPIOA)
        ↓
  [STM32H533]
  Centrale processor
  - Stuurt MCP aan via SPI
  - Genereert MIDI
  - USB verbinding
        ↓
  [USB KABEL]
        ↓
  [COMPUTER/DAW]
  Ontvat MIDI noten
```

### 1.2 Data Flow: Knopdruk → MIDI-bericht

| Stap | Wat | Hoe |
|------|-----|-----|
| **1** | Knop ingedrukt | Contact gemaakt in matrix |
| **2** | Matrix scanning | STM32 stuurt kolom LAAG via MCP GPIOA |
| **3** | Rij detectie | STM32 leest MCP GPIOB → ziet welke rij ook LAAG |
| **4** | Debounce | 5× hetzelfde = betrouwbaar (100ms wachten) |
| **5** | Verandering | `prev_state=1` → `current_state=0` => KNOP IN |
| **6** | MIDI Note On | Bericht: `[0x90, NOOT, 127]` → USB |
| **7** | DAW speelt** | Synthesizer genereert geluid |

### 1.3 Software Lagen (Functies)

```
┌─────────────────────────────────┐
│  APPLICATIE LAAG                │
│  • keypad_task() - matrix scan  │
│  • midi_task() - MIDI verwerken │
│  • led_blinking_task()          │
├─────────────────────────────────┤
│  DRIVER LAAG                    │
│  • mcp23s17_write_reg()         │
│  • mcp23s17_read_reg()          │
│  • spi_transfer_byte()          │
├─────────────────────────────────┤
│  HAL LAAG                       │
│  • HAL_GPIO_WritePin()          │
│  • HAL_GPIO_ReadPin()           │
│  • HAL_GetTick()                │
├─────────────────────────────────┤
│  HARDWARE                       │
│  • GPIO pins, SPI, USB          │
└─────────────────────────────────┘
```

**Waarom deze lagen?**
- **Separatie van concerns** - elke laag één job
- **Herbruikbaarheid** - driver werkt met elke applicatie
- **Maintainability** - makkelijker debuggen

### 1.4 Initialisatievolgorde (WAAROM BELANGRIJK!)

```
1. HAL_Init()              → STM32 basis drivers
2. SystemClock_Config()    → Systeem klok 4MHz
3. MX_GPIO_Init()          → GPIO pinnen instellen (SPI pins!)
4. MX_SPI_BitBang_Init()   → SPI handmatig geconfigureerd
5. MX_USB_Init()           → USB hardware gereed
6. mcp23s17_init()         → MCP via SPI configureren
7. tusb_init()             → TinyUSB stack starten
8. Main loop               → USB task ALTIJD EERSTE
```

**Waarom niet omgekeerd?**
- Stap 5 nodig voordat stap 6 werkt
- SPI pins moeten eerst ingesteld zijn
- USB interrupt moet actief zijn

---

## 2. MIDI USB CLASS

### 2.1 MIDI Basis (Wat Is Het?)

**MIDI = Musical Instrument Digital Interface**

| Kenmerk | Inhoud |
|---------|--------|
| **Doel** | Standaard voor muziekinstrumenten die praten |
| **Snelheid** | 31.250 bits per seconde (serieel) |
| **Kanalen** | 16 kanalen per apparaat (0-15 / 1-16 in DAW) |
| **Type** | 8-bit berichten, digitaal |

**Analogie:** MIDI is als "speel noot 60" texten naar een synthesizer. USB MIDI stuurt die berichten via USB.

### 2.2 MIDI Berichtstructuur

Elk MIDI-bericht = **Status byte + Data bytes**

#### Note On (Knop ingedrukt)
```
Byte 1: 0x90          = Note On status (channel 0)
Byte 2: 60-127        = MIDI nootnummer
Byte 3: 0-127         = Velocity (aanslagnelheid)
                        127 = maximum snelheid
```

**Voorbeeld:** Noot 60 indrukken met volle kracht
```
0x90  60  127
```

#### Note Off (Knop losgelaten)
```
Byte 1: 0x80          = Note Off status (channel 0)
Byte 2: 60-127        = ZELFDE nootnummer
Byte 3: 0-127         = Release velocity (meestal 0)
```

**Voorbeeld:** Noot 60 loslaten
```
0x80  60  0
```

#### Control Change (CC) - extra
```
Byte 1: 0xBn          = CC status
Byte 2: 0-127         = CC nummer (volume, terugslag, etc.)
Byte 3: 0-127         = CC waarde
```

### 2.3 Channels (Kanalen)
- `0x90` = Note On channel **0** (DAW toont: channel 1)
- `0x91` = Note On channel **1** (DAW toont: channel 2)
- Tot `0x9F` = Note On channel **15** (DAW toont: channel 16)

**Dit project:** Altijd channel 0 (0x90)

### 2.4 USB MIDI Class (Plug & Play)

| Aspect | Wat Dit Betekent |
|--------|-----------------|
| **Geen drivers** | Windows/Mac/Linux herkennen automatisch |
| **USB Audio Device** | Verschijnt als "USB MIDI Input" in DAW |
| **USB Descriptor** | Vertelt host "ik ben MIDI" |
| **Class compliant** | Werkt met ELKE DAW zonder setup |

**Praktijk:** Plug in → DAW ziet apparaat → Koel!

---

## 3. SPI COMMUNICATIE

### 3.1 SPI Protocol (De 4 Lijnen)

```
Master (STM32)          Slave (MCP23S17)
    ┌─────────┐              ┌─────────┐
    │         │              │         │
    │   SCK ──┼──────────────┼──→ CLK  │  KLOK (Master)
    │   MOSI ─┼──────────────┼──→ IN   │  Data Out (Master → Slave)
    │   MISO ←┼──────────────┼─── OUT  │  Data In (Master ← Slave)
    │   CS  ──┼──────────────┼──→ CS   │  Chip Select (actief LAAG)
    │         │              │         │
    └─────────┘              └─────────┘

SC = Serial Clock   (Master genereert klok)
MOSI = Master Out Slave In (Master stuurt, Slave ontvangt)
MISO = Master In Slave Out (Slave reageert, Master leest)
CS = Chip Select (STM32 zegt: "jij bent geselcteerd!")
```

**Synchrone communicatie:** Data wordt netjes gesynchroniseerd met klok.

### 3.2 CS (Chip Select) - Belangrijk!

**Chip Select werkt met ACTIEF LAAG:**

```
Normale staat:    CS = HOOG (1)  → MCP luistert niet
Actief:           CS = LAAG (0)  → MCP luistert naar SPI
Transactie klaar: CS = HOOG (1)  → MCP stopt luisteren
```

**Voorbeeld:**
```c
CS_LOW();                    // MCP: "oké, ik luister"
spi_transfer_byte(0x40);    // Stuur opcode
spi_transfer_byte(0x12);    // Stuur register GPIOA
spi_transfer_byte(data);    // Stuur/ontvang data
CS_HIGH();                   // MCP: "klaar, terug slaap"
```

### 3.3 MCP23S17 SPI Transactie (3 Bytes)

**Elke SPI communicatie = Opcode + Register + Data**

```
BYTE 1: Opcode
        0100 AAA R
        │    │││ └─ R = 0 (schrijven) / 1 (lezen)
        │    └─── AAA = Chip adres (0-7, meestal 0)
        └──────── 0100 = Standaard

BYTE 2: Register adres
        0x00 = IODIRA, 0x12 = GPIOA, 0x13 = GPIOB, etc.

BYTE 3: Data
        Waarde om te schrijven OF waarde die je ontvangt
```

#### Standaard Opcodes (A2=A1=A0=0)

| Actie | Opcode | Binair |
|-------|--------|--------|
| **Schrijven** | 0x40 | 0100 0000 |
| **Lezen** | 0x41 | 0100 0001 |

**Voorbeeld 1: GPIOA instellen op 0x0F (alle kolommen HI)**
```
CS LOW
Transfer: 0x40 (= schrijf opcode)
Transfer: 0x12 (= GPIOA register)
Transfer: 0x0F (= waarde: alle kolommen hoog)
CS HIGH
```

**Voorbeeld 2: GPIOB uitgelezen (welke rijen zijn laag?)**
```
CS LOW
Transfer: 0x41 (= lees opcode)
Transfer: 0x13 (= GPIOB register)
received = Transfer: 0x00 (dummy, maar slaat respons op)
CS HIGH
Result: received = 0xF2 (bijv. betekent: rij 2,3,4 laag)
```

### 3.4 Belangrijke MCP23S17 Registers

| Register | Adres | Functie | Waarden |
|----------|-------|---------|---------|
| **IODIRA** | 0x00 | I/O richting PORTA | 0=output, 1=input |
| **IODIRB** | 0x01 | I/O richting PORTB | 0=output, 1=input |
| **GPPUA** | 0x0C | Pull-up PORTA | 0=uit, 1=aan (100kΩ) |
| **GPPUB** | 0x0D | Pull-up PORTB | 0=uit, 1=aan |
| **GPIOA** | 0x12 | GPIO waarde PORTA | Lezen/schrijven pins |
| **GPIOB** | 0x13 | GPIO waarde PORTB | Lezen/schrijven pins |

**Dit project:**
- IODIRA = 0x00 (PORTA = outputs voor kolommen)
- IODIRB = 0x0F (PORTB = inputs voor rijen)
- GPPUB = 0x0F (Pull-ups AAN op rijen)

---

## 4. BUTTON DEBOUNCING

### 4.1 Contact Dender (Bouncing) - Waarom?

**Mechanische knoppen zijn niet perfect:**

```
Knop ingedrukt:
Signal:  ┌──────────────
         │    ooooo ooo o  ← Bounces (ruis)
         └──────────────

Tijd: 0ms  5ms  10ms  15ms  20ms  25ms

Probleem: STM32 zou 5-10 Nederlandse aparte "press" events zien!
Result: MIDI Note On 10× achter elkaar voor 1 knopdruk!
```

**Bounce Duration:** Typisch 5-50ms van contact tot stabiel

### 4.2 Debounce Oplossing (Software Timing)

**Principe:** Als de sensor 5 keer hetzelfde antwoord geeft = betrouwbaar

```c
#define DEBOUNCE_COUNT 5    // Aantal identity readings
#define SCAN_INTERVAL 20    // Scan elke 20ms dus

// 5 × 20ms = 100ms stabiliteitsvenster
// Bewegende draden geven NOOIT 100ms lang dezelfde waarde
```

**Timeflow:**
```
Tijd:        0ms    20ms   40ms   60ms   80ms   100ms  120ms
Scan:        1      1      1      1      1      1      1
Raw:         1→0    0→1    0      0      0      0      0
Candidate:   X      1      0      0      0      0      0
Count:       0      1      1      2      3      4      5
Accepted:    1      1      1      1      1      1      0 ✓

Bij 5 accepteren = KNOP IN
```

### 4.3 Debounce Code Logica

```c
if (rawReading != candidate) {
    candidate = rawReading;     // Nieuw getal?
    count = 1;                  // Restart teller
} else if (count < 5) {
    count++;                    // Zelfde getal, tel op
    if (count >= 5)
        acceptedState = rawReading;  // Stabiel!
}
```

### 4.4 State Change Detection (Crucciaal voor MIDI!)

**Niet de toestand zelf, MAAR de VERANDERING!**

| Vorig | Huiding | Overgang | MIDI Actie |
|-------|---------|----------|-----------|
| 0 | 0 | — | Niets |
| 0 | 1 | 0→1 (losgelaten) | **Note Off** 0x80 |
| 1 | 1 | — | Niets |
| 1 | 0 | 1→0 (ingedrukt) | **Note On** 0x90 |

**Dit project gebruikt:**
```c
if (prev_bit == 1 && current_bit == 0) {
    // 1→0: knop NET ingedrukt
    send_midi_note_on(note);
}
else if (prev_bit == 0 && current_bit == 1) {
    // 0→1: knop NET losgelaten
    send_midi_note_off(note);
}
```

---

## 5. MATRIX SCANNING

### 5.1 Hoe een 4x4 Matrix Werkt

**16 knoppen met slechts 8 draden! (Slimheid)**

```
Kolommen (OUTPUTS van GPIOA):
├─ KOL0 (GPA0)
├─ KOL1 (GPA1)
├─ KOL2 (GPA2)
└─ KOL3 (GPA3)

Rijen (INPUTS van GPIOB):
├─ RIJ0 (GPB0)
├─ RIJ1 (GPB1)
├─ RIJ2 (GPB2)
└─ RIJ3 (GPB3)

Matrix:
            KOL0  KOL1  KOL2  KOL3
        ┌───┬───┬───┬───┐
  RIJ0  │ 0 │ 1 │ 2 │ 3 │
        ├───┼───┼───┼───┤
  RIJ1  │ 4 │ 5 │ 6 │ 7 │
        ├───┼───┼───┼───┤
  RIJ2  │ 8 │ 9 │10 │11 │
        ├───┼───┼───┼───┤
  RIJ3  │12 │13 │14 │15 │
        └───┴───┴───┴───┘

Logica: Stuur KOL0 LAAG → Lees welke RIJ ook LAAG
        Knop i waar beide LAAG = INGEDRUKT
```

### 5.2 Scanprocedure (Stap voor Stap)

```
ROUND 1:
├─ Stap 1: GPIOA = 0xFE (11111110, kolom 0 LAAG)
├─ Stap 2: GPIOB ingelezen = bijv. 0xF0
│          Vorstaat: RIJ0 ontvangt negatieve teen
├─ Stap 3: Betekent knop 0 (RIJ0 × KOL0) ingedrukt
│
ROUND 2:
├─ Stap 1: GPIOA = 0xFD (11111101, kolom 1 LAAG)
├─ Stap 2: GPIOB ingelezen = bijv. 0xF5
│          RIJ0 en RIJ2 laag
├─ Stap 3: Knoppen 4 (RIJ1×KOL1) en 8 (RIJ2×KOL1) ingedrukt
│
ROUND 3: GPIOA = 0xFB (11111011, kolom 2 LAAG)
ROUND 4: GPIOA = 0xF7 (11110111, kolom 3 LAAG)
ROUND 5: GPIOA = 0x0F (11111111, ALLE KOLOMMEN HI - reset)
```

### 5.3 Scancode (Pseudocode)

```c
void scan() {
    for (int col = 0; col < 4; col++) {
        // Stuur alleen deze kolom LAAG
        pattern = 0x0F; // Alle HI
        pattern &= ~(1 << col); // Kolom LAAG
        write_gpioa(pattern);
        
        // Lees rijen
        rows = read_gpiob();
        
        // Elke bit in rows = één rij
        for (int row = 0; row < 4; row++) {
            bit = (rows >> row) & 1;
            keypad_state[col][row] = bit;
        }
    }
    // Zet alles terug HI
    write_gpioa(0x0F);
}
```

### 5.4 Ghosting (Bonus Knowledge)

**Wat:** Bij simultaan indrukken van multiple knoppen kunnen "ghost" knoppen verschijnen

**Voorbeeld:**
```
Ingedrukt: Knopen 0 (RIJ0-KOL0) en 5 (RIJ1-KOL1)

Zonder antialiasing ziet STM32 ook:
- Knop 1 (RIJ0-KOL1) - GHOST!
- Knop 4 (RIJ1-KOL0) - GHOST!

Oplossing: Diode per knop OF ghosting in software toestaan
```

Dit project: **Geen diodes → ghosting is normaal** bij 3+ gelijktijdige knoppen.

---

## 6. CODE SNIPPET UITLEG (Wat Aconteert)

### 6.1 Initialisatie
```c
mcp23s17_init() {
    // GPIOA = kolommen → outputs (we sturen ze aan)
    write_reg(IODIRA, 0x00);    // 0 = output

    // GPIOB = rijen → inputs (we lezen ze)
    write_reg(IODIRB, 0x0F);    // 0x0F = alle bits input
    write_reg(GPPUB,  0x0F);    // Pull-ups AAN (als niemand drukt = HI)
}
```

**Wat:** Zegt tegen MCP: "Kolommen zijn my outputs, rijen zijn mijn inputs"

### 6.2 SPI Transfer
```c
uint8_t spi_transfer_byte(uint8_t data) {
    for (int i = 7; i >= 0; i--) {  // MSB first
        // Zet bit
        if (data & (1 << i))
            gpio_set_mosi();
        else
            gpio_clear_mosi();
        
        // Klokpuls
        gpio_set_sck();     // Stijging
        bit = read_miso();  // Slave reageert
        if (bit) received |= (1 << i);
        gpio_clear_sck();   // Daling
    }
    return received;
}
```

**Wat:** Handmatig 1 byte over SPI sturen en ontvangen.

### 6.3 Matrix Scan
```c
for (int row = 0; row < 4; row++) {
    // Pattern: bijv row=0 → 1110 = 0xE
    uint8_t pattern = ~(1 << row) & 0x0F;
    write_gpioa(pattern);           // Stuur richting MCP
    
    uint8_t read1 = read_gpiob();   // Eerste read
    uint8_t read2 = read_gpiob();   // Controle read
    
    // Debounce: beide hetzelfde?
    if (read1 == read2 && read1 == candidate[row]) {
        count[row]++;
        if (count[row] >= 5)
            state[row] = read1;     // Stabiel accepteren
    }
}
```

**Wat:** Scant kolom per kolom, filtert ruis met debounce.

### 6.4 MIDI Generatie
```c
for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
        curr = (state[row] >> col) & 1;
        prev = (prev_state[row] >> col) & 1;
        
        if (prev == 1 && curr == 0) {  // 1→0: indrukken
            uint8_t note = note_map[row][col];
            midi_note_on[4] = {0x09, 0x90, note, 127};
            tud_midi_packet_write(midi_note_on);
        }
        else if (prev == 0 && curr == 1) {  // 0→1: loslaten
            midi_note_off[4] = {0x08, 0x80, note, 0};
            tud_midi_packet_write(midi_note_off);
        }
    }
}
```

**Wat:** Detecteert state changes, stuurt MIDI berichten.

---

## 7. MIDI NOTE MAPPING

### 7.1 Formule

$$\text{MIDI Noot} = \text{BASIS} + (\text{rij} \times 4) + \text{kolom}$$

Basis = 60 (Middle C / C4)

### 7.2 Volledige Tabel

| | KOL0 | KOL1 | KOL2 | KOL3 |
|------|------|------|------|------|
| **RIJ0** | 60 C4 | 61 C#4 | 62 D4 | 63 D#4 |
| **RIJ1** | 64 E4 | 65 F4 | 66 F#4 | 67 G4 |
| **RIJ2** | 68 G#4 | 69 A4 | 70 A#4 | 71 B4 |
| **RIJ3** | 72 C5 | 73 C#5 | 74 D5 | 75 D#5 |

**Dit project gebruikt (anders, meer musicaal):**

| | KOL0 | KOL1 | KOL2 | KOL3 |
|------|------|------|------|------|
| **RIJ0** | 60 C4 | 62 D4 | 64 E4 | 65 F4 |
| **RIJ1** | 67 G4 | 69 A4 | 71 B4 | 72 C5 |
| **RIJ2** | 74 D5 | 76 E5 | 77 F5 | 79 G5 |
| **RIJ3** | 81 A5 | 83 B5 | 84 C6 | 86 D6 |

---

## 8. KEY POINTS VOOR TOETS

### Begrijpen (NIET Memoriseren!)

✅ **Waarom** debouncing nodig is (contact ruis)
✅ **Waarom** state-change detection (niet toestand zelf)
✅ **Waarom** SPI Chip Select actief LAAG
✅ **Waarom** kolommen outputs + rijen inputs (not omgekeerd!)
✅ **Waarom** initialisatie in bepaalde volgorde

### MIDI Kern

- Note On = 0x90
- Note Off = 0x80
- 3 bytes: Status + Note + Velocity

### SPI Kern

- 4 lijnen: SCK, MOSI, MISO, CS
- CS LAAG = actief → MCP luistert
- 3 bytes per transactie: Opcode + Register + Data

### Matrix Kern

- 4 kolommen (outputs) × 4 rijen (inputs) = 16 knoppen
- Scan: stuur kolom LAAG → lees welke rijen LAAG
- Debounce: 5× hetzelfde gelding accepteren

---

## 9. VEELGESTELDE VRAGEN

**V: Waarom USB MIDI en niet gewoon USB HID?**
A: MIDI is standaard voor synthesizers, HID voor toetsenborden. MIDI = beter voor dit doel.

**V: Waarom niet hardware SPI?**
A: Dit project gebruikt bit-bang (software SPI) omdat het simpel is en genoeg snelheid heeft.

**V: Wat gebeurt als knop "bounceert" 10× in 50ms?**
A: Debounce vereist 5× dezelfde waarde in 100ms totaal → ruis wordt genegeerd.

**V: Kan ik 2 knoppen tegelijk indrukken?**
A: Ja! Matrix scant alle tegelijk. Ghosting kan. MIDI stuurt beide Note Ons.

**V: Waarom 0x90 en niet 0x9F (ander kanaal)?**
A: Dit project gebruikt kanaal 0 (0x9**0**). 0x9F = kanaal 15.

**V: Stel CS gaat kapot (altijd LAAG)?**
A: MCP17 reageert ALTIJD op SPI. Mogelijke data corruption.

---

## 10. OEFENVRAGEN VOOR STUDIE

**Vraag 1:** Schets de volledige dataflow van knopdruk tot MIDI in DAW.

**Vraag 2:** Leg uit waarom debouncing nodig is EN hoe het werkt.

**Vraag 3:** Tekening: MCP23S17 pinout + alle SPI verbindingen naar STM32.

**Vraag 4:** Welke registers moet je instellen bij MCP23S17 init? Waarom?

**Vraag 5:** MCP23S17 zit in LAAGSTAND, STM32 schrijft 0xC0 naar GPIOA. Wat gebeurt?

**Vraag 6:** MIDI-bericht: `0x90 64 100`. Wat betekent dit?

**Vraag 7:** Leg verschil uit: toestand vs. state change detection.

**Vraag 8:** Waarom moet `tud_task()` ALTIJD eerst in main loop?

**Vraag 9:** Hoe werkt de 4x4 matrix met slechts 8 pinnen?

**Vraag 10:** SPI transactie om GPIOB in te lezen. Schrijfde 3 bytes.

---

## 11. KERNFORMULES

| Concept | Formule |
|---------|---------|
| **MIDI Noot** | 60 + (rij × 4) + kolom |
| **SPI Opcode Schrijven** | 0x40 (0100 0000) |
| **SPI Opcode Lezen** | 0x41 (0100 0001) |
| **Debounce tijd** | DEBOUNCE_COUNT × SCAN_INTERVAL = 5 × 20ms = 100ms |
| **Matrix grootte** | 4 kolommen × 4 rijen = 16 toetsen |

---

## 12. STUDIE TIPS

💡 **Visual Learner Tips:**
- Teken dataflow diagram meerdere keren
- Kleur code: output=rood, input=blauw, MIDI=groen
- Geheugenkaart: CENTER="USB MIDI" → takken naar componenten

💡 **Begrijp de "Waarom":**
- Niet: "Schrijf 0x40 naar SPI"
- Wel: "0x40 is opcode = bit 7 laag betekent SCHRIJVEN, bits 6-0 = adres/data"

💡 **Oefenen:**
- Teken voltage-timings voor SPI transactie
- Werk scan-voorbeeld met andere waarde door
- Bedenk wat gebeurt bij fouten (CS stuck, reversed MOSI/MISO)

---

**SUCCES MET JE TOETS! JE BENT ER KLAAR VOOR!** 🚀