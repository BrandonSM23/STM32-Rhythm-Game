# STM32 Rhythm Game

A Guitar Hero–style rhythm game engine running bare-metal on an **STM32F446RE (ARM Cortex-M4)**, written in C with the STM32 HAL.

Notes are scheduled on a millisecond timeline. When the player presses a button, the firmware works out how early or late the press was, grades it **PERFECT** or **GOOD**, and updates the score and combo. A microSD card (FatFs over SPI) is wired up so songs can eventually be loaded from files. An 8×32 LED matrix display is in progress.

> **Status:** work in progress. The timing and scoring engine has been tested on the board. The SD card driver is written but not yet re-verified on hardware. The LED matrix is at the "first SPI write" stage. See [Current status](#current-status).

---

## Hardware

| Part | Details |
|---|---|
| Board | ST **NUCLEO-F446RE** (STM32F446RET6, Cortex-M4F, 512 KB flash / 128 KB RAM) |
| Clock | 16 MHz HSI → PLL → **84 MHz** SYSCLK (APB1 = 42 MHz, APB2 = 84 MHz) |
| Input | On-board USER button (B1, **PC13**) on an EXTI interrupt |
| Feedback LED | On-board LD2 (**PA5**), toggles on every successful hit |
| Storage | microSD card breakout on **SPI2** |
| Display | **MAX7219 8×32 LED matrix** (4-in-1 module: four daisy-chained MAX7219 8×8 drivers) on **SPI3** (in progress) |
| Debug output | **USART2** → ST-LINK virtual COM port, 115200 8N1, `printf` retargeted |

### Pin map

| Function | Peripheral | Pins |
|---|---|---|
| Serial debug | USART2 | PA2 (TX), PA3 (RX), via ST-LINK USB |
| microSD | SPI2 (master, full duplex) | PB13 SCK, PB14 MISO, PB15 MOSI, **PB12 CS** (software) |
| LED matrix (MAX7219 ×4) | SPI3 (master, TX only) | PC10 SCK → CLK, PC1 MOSI → DIN, **PC12 → CS/LOAD** (software) |
| Hit button | GPIO EXTI line 13 | PC13 (USER button) |
| Hit LED | GPIO output | PA5 (LD2) |

---

## How the timing and scoring work

The song is an array of notes. Each note has a target time in milliseconds, measured from the start of the song:

```c
typedef struct {
    uint32_t     timestamp_ms;   // when the note should be hit
    uint8_t      fret_id;        // which button/lane (single lane for now)
    NoteStatus_t status;         // NOTE_UNHIT / NOTE_HIT / NOTE_MISSED
} Note_t;
```

`HAL_GetTick()` (1 ms SysTick) is the game clock. The song starts when `song_start_time = HAL_GetTick()` is recorded.

**On every button press** (`HAL_GPIO_EXTI_Callback`, debounced to 100 ms):

1. `elapsed = press_time - song_start_time`
2. For the first note that hasn't been hit, compute `delta = elapsed - note.timestamp_ms`
   * negative → pressed early, positive → pressed late
3. Grade the press on `|delta|`:

```
                     note.timestamp_ms
                             │
   ◄──────── GOOD ─────────┬─┴─┬───────── GOOD ────────►
            -120 ms   -50 ms   +50 ms   +120 ms
                         PERFECT
```

| Window | Result | Score | Combo |
|---|---|---|---|
| \|delta\| ≤ 50 ms | **PERFECT** | +100 | +1 |
| \|delta\| ≤ 120 ms | **GOOD** | +50 | +1 |
| No note within ±120 ms | **EXTRA TAP** | — | reset to 0 |

The notes are stored in time order, so the search stops as soon as it reaches a note that is still more than 120 ms in the future.

**In the main loop**, any note that is still un-hit more than 120 ms after its target time is marked **MISSED**, which resets the combo. Two seconds after the last note, the game prints the final score.

---

## microSD card (SPI + FatFs)

The card runs in **SPI mode** on SPI2. FatFs (ChaN, R0.12c, as packaged by CubeMX) handles the FAT file system. Below it, `FATFS/Target/user_diskio.c` is a custom low-level driver that implements FatFs's disk interface:

* `USER_initialize`: SD SPI-mode power-up sequence: ≥74 dummy clocks, `CMD0` (go idle), `CMD8` (voltage check), an `ACMD41` loop until the card is ready, then `CMD58` to read the OCR and tell SDHC/SDXC (block-addressed) from SDSC (byte-addressed) cards
* `USER_read` / `USER_write`: single-block `CMD17` / `CMD24` transfers of 512-byte sectors, with data-token, data-response and busy-wait handling
* Initialisation runs at **~656 kHz** (cards must start slow). The SPI prescaler is then raised to **~10.5 MHz** for data transfer.

At boot, the firmware mounts the card, writes `test.txt`, reads it back and prints how long the read took over serial.

---

## Building and flashing

**Tools:** STM32CubeIDE 2.2.0 (GNU Tools for STM32 14.3), STM32CubeMX 6.18.1, STM32Cube FW_F4 V1.28.3

1. Clone the repo:
   ```bash
   git clone https://github.com/BrandonSM23/STM32-Rhythm-Game.git
   ```
2. In STM32CubeIDE: **File → Import → General → Existing Projects into Workspace**, then select the cloned folder. The project imports as **`First`**.
3. Build: **Project → Build Project** (hammer icon). Output goes to `Debug/`, which is git-ignored.
4. Plug in the Nucleo over USB. Click **Run** (or **Run → Run As → STM32 C/C++ Application**) and accept the default ST-LINK settings. Debug configurations aren't committed, so CubeIDE creates one on the first run.
5. Open a serial terminal (PuTTY, Tera Term, or CubeIDE's terminal) on the ST-LINK COM port at **115200 baud**.
6. Press the blue USER button in time with the notes. Press the black RESET button to replay.

Pin and clock settings live in `First.ioc`. If you change them in CubeMX and regenerate, only code inside `/* USER CODE BEGIN */ … /* USER CODE END */` blocks is kept.

### Example serial output

Illustrative. This shows the log format, not a real capture.

```
=== System Boot: Testing MicroSD Write & Read ===
[SD INIT] CMD0 returned: 0x01 (Expected <= 0x01)
[SD INIT] ACMD41 returned: 0x00 (Expected: 0x00)
[SD OK] Card mounted successfully!
[SD OK] Wrote 32 bytes to test.txt
...
=== System Boot: Rhythm Engine Initialized ===
Track loaded: 5 notes.
[PERFECT] Target: 3000 ms | Delta: +23 ms | Score: 100 | Combo: 1
[GOOD]    Target: 5000 ms | Delta: -87 ms | Score: 150 | Combo: 2
[MISSED]  Target: 7000 ms passed unhit! Combo reset.
```

---

## Project layout

```
Core/Src/main.c               Game engine: note track, hit windows, scoring, SD test, peripheral init
Core/Src/stm32f4xx_it.c       Interrupt handlers (SysTick, EXTI15_10 for the button)
Core/Src/stm32f4xx_hal_msp.c  Pin/clock setup for SPI2, SPI3, USART2
FATFS/Target/user_diskio.c    SD card SPI driver (FatFs disk I/O layer)
FATFS/App/fatfs.c             Links the driver into FatFs
First.ioc                     CubeMX configuration (pins, clocks, peripherals)
Drivers/, Middlewares/        ST HAL, CMSIS, Nucleo BSP, FatFs (vendor code)
STM32F446RETX_FLASH.ld        Linker script
```

---

## Current status

**Tested on hardware**
- [x] 84 MHz clock tree, USART2 `printf` debug logging
- [x] Interrupt-driven button input with 100 ms debounce
- [x] Timing engine: PERFECT / GOOD / MISS / extra-tap detection, score and combo
- [x] Hard-coded 5-note test track (single lane)

**Written, needs hardware verification**
- [ ] Custom SD card SPI driver plugged into FatFs: mount, write and read-back test with read-time measurement

**In progress**
- [ ] MAX7219 8×32 LED matrix on SPI3: CubeMX configuration (SPI3 + PC12 chip select) is done, and a first test write is in `main.c`. Next steps:
  - send full 16-bit MAX7219 frames (register address + data)
  - toggle CS/LOAD around each transfer
  - lower the SPI3 clock to within the MAX7219's 10 MHz limit
  - run the init sequence (shutdown, scan limit, decode mode, intensity) for all four chained chips

## Roadmap

- [ ] Drive the LED matrix: falling notes scrolling down the lanes and a hit/miss flash
- [ ] Load songs from the SD card (a simple text/CSV chart of `timestamp_ms,lane`) instead of a hard-coded array
- [ ] Multiple lanes with external fret buttons, so `fret_id` is actually used
- [ ] Move scoring out of the interrupt: the ISR only timestamps presses into a queue, and the main loop does the grading and printing
- [ ] Use a hardware timer instead of the 1 ms SysTick for finer timing
- [ ] Audio cue/metronome output (PWM or DAC) so the player has something to play along to
- [ ] Start/restart button and a score screen on the display

---

## Author

**Brandon Mendez**, Computer Engineering (Co-op), University of Alberta
