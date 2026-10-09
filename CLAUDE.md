# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

Guará is a virtual-pet / RPG handheld built on an ESP32 (NodeMCU-32S) with a 128x64 1-bit OLED (SH1106 I2C), buzzer, MicroSD and 6 buttons. The firmware implements every menu in the README (phase status in `docs/PLAN.md`).

- `README.md` — the design spec (in Portuguese) and the source of truth: hardware pinout, partitions, save-state layout, UI standards, menus, combat formulas, life cycle, sprites, Lua API. Its last section, **Implementação**, holds the project rules (summarized below).
- `docs/GAME_DESIGN.md` — numbers and behaviours the README leaves open (decay rates, XP curve, stage levels, extra combat formulas, unspecified controls). Values live in `lib/core/src/rules.h`; keep both in sync.
- `docs/PLAN.md` — phase plan, hardware decisions (D1–D10) and the README fix backlog (H/I/L/A/C items). Mark items there when you resolve them.
- `catalog/items.csv`, `equipment.csv`, `skills.csv` — packer input. They hold **only** what the author defined in `CATALOG.md`, converted by hand (today: the Maçã). Never invent catalog entries; fields missing in `CATALOG.md` use the neutral default and must be pointed out to the author. Adding catalog items is a MINOR version bump. Tests use `test/fixtures/catalog/`.
- `bitmaps/` — 1-bit VROM assets. `examples/sdcard/games/` — sample Lua games: `apple_catch` (short API demo) and `boitata` (the reference game, evolved together with the author). `sdcard/` — the author's own SD card content (games, music, saves).

## Project rules (README > Implementação)

- **Code:** identifiers, functions, files and folders are descriptive and in **English**; comments are in **Portuguese** and abundant: a header comment per file and a comment before every function. UI text shown on the device stays in Portuguese.
- **Versioning:** `v#.#.#` = release.feature.patch (`-DGUARA_VERSION` in `platformio.ini`, history in `CHANGELOG.md`). Bump only for firmware or storage changes; documentation-only changes are just committed.
- **Every executed scope** gets its own commit and a `CHANGELOG.md` entry.
- **0.x.x builds** unlock the whole catalog (every item at 255 units, all equipment and skills) at birth and on every boot (`guara_is_development_version` / `guara_unlock_catalog`).

**Side convention:** whenever the README says "direita"/"esquerda", it means the **device's** side — the device's right is the user's left (e.g. the clock "no canto superior direito" is drawn at the viewer's top-left). Pixel coordinates are plain screen coordinates (X=0 at the viewer's left). **Exception:** the d-pad buttons are named by screen direction from the user's view (button "Direita", GPIO27, moves +X). In code, `KEY_LEFT`/`KEY_RIGHT` are screen directions; the GPIO mapping is in `src/hal/pins.h`. Check `bitmaps/exemple/*.bmp` mockups when in doubt.

## Commands

`pio` is not on PATH; use `~/.platformio/penv/bin/pio`. The board is on `/dev/ttyACM0` (CH343 adapter).

```bash
pio run -e esp32                          # build firmware (must stay warning-free, -Wshadow included)
pio run -e esp32 -t upload                # flash bootloader + partition table + app
pio run -e esp32 -t upload_storage        # pack bitmaps/ + catalog/ and flash only the storage partition
pio run -e esp32 -t build_storage         # just generate .pio/storage.bin
pio test -e native                        # all host tests (Unity)
pio test -e native -f test_core           # one test folder (test_crc32, test_gfx, test_core, test_midi_ini)
python3 tools/font-builder/build_font.py  # regenerate lib/gfx/src/font_5x8.c from font_5x8.txt
python3 tools/bmp-builder/bmp_builder.py in.png -o bitmaps/icons/x.bmp --preset icon --method otsu -p
```

`test_core` reads `.pio/storage_test.bin`, generated automatically from `test/fixtures/catalog/` by the `native` pre-script `tools/pio_storage_test.py`.

`bmp_builder.py` converts PNG/JPG/SVG/… to the canonical 1-bit BMP. Its presets are icon 16x16, sprite 32x32, mannequin 32x56, boss 64x64 and screen 128x64; `-p` prints a preview. A C++ port builds with `make` in `tools/bmp-builder`.

**Serial debug console** (`tools/console.py`, run with `~/.platformio/penv/bin/python`): opening the port **resets the board** on this adapter, so run one `script` session per test, for example `console.py script "wait 4" "keys jsk" "screen out.png" "state"`. With Wi-Fi on, boot finishes in about 2 s; the network connects later in the background.
- Keys: `w s a d` = d-pad (screen directions), `j` = A, `k` = B.
- `P` dumps the framebuffer; `S` prints the pet state.
- Never open the port with DTR asserted: GPIO0 goes low and the board enters download mode.
- The user may be pressing physical buttons at the same time.
- After 30 s without keys the device enters Idle mode (light sleep, dim screen): the first key only wakes it and is swallowed. `console.py` sends a wake preamble before each command, since the UART bytes that wake the chip are lost.

## Code layout

- `src/` — ESP-IDF main component (hardware/IDF code only). `CMakeLists.txt` lists the `REQUIRES` and the include dirs (`hal`, `system`, `ui`, `screens`).
  - `hal/` — device drivers:
    - `display`: SH1106; an async task sends only the changed pages;
    - `buttons`: event queue with debounce and repeat;
    - `audio`: LEDC task playing effects plus a background `music_source_t`;
    - `battery`, `sdcard`, `rtc_clock` (DS3231/NTP/TZ), `power` (deep sleep);
    - `wifi`: raw driver, used only through `system/network`;
    - `i2c`, `pins.h`.
  - `system/` — services:
    - `app`: boot, main loop, per-minute life cycle, inactivity sleep, battery emergency;
    - `persistence`: storage mmap, save ring, RTC-RAM mirror;
    - `config`: NVS;
    - `network`: Wi-Fi on demand. Never call `wifi_*` from screens; use `network_use`/`network_wait`/`network_release`, `network_run` for background jobs, and `network_suspend` around memory-hungry tasks;
    - `alarms`, `ota`, `debug_console`.
  - `ui/` — screen stack (`ui.h`), standard widgets (`widgets.h`), modals and virtual keyboard (`modal.h`).
  - `screens/` — one file per menu; `screens.h` lists them. `submenu.h` builds static standard sub-menus; `sprite.h` composes head + body + eyes.
  - Lua API: `games.c` holds the VM runtime and the base modules (`oled`, `btn`, `audio`, `guara`, `storage`); `game_api.c` adds VROM drawing options, `guara.draw`, `catalog` and the `ui` widgets/modals. Keep the README API table in sync when changing either.
- `lib/` — pure C11, **no IDF includes**, compiled for both `esp32` and `native`:
  - `crc32`;
  - `gfx`: page-format framebuffer, primitives, UTF-8 text, draw modes, BMP decoder;
  - `core`: types, storage/VROM/catalog reader, save format and ring, pet life cycle, combat;
  - `midi`: SMF → monophonic;
  - `ini`;
  - `lua`: vendored 5.4.7 with `LUA_32BITS` and no io/os/package/debug; excluded from `native`.

UI patterns:
- A screen is a `screen_t` with callbacks (`on_open`, `on_resume`, `on_close`, `on_key`, `on_draw`). Reusable screens that can be stacked (modals, `submenu`) keep per-layer state indexed by `ui_current_layer()`.
- Modal results arrive through a callback *after* the modal has been popped, so the callback may open new screens.
- Call `app_mark_changed()` after changing `g_guara`. It mirrors to RTC RAM and sets the dirty flag; the flash save happens when returning to the menu or main screen. Use `app_save()` for critical events.

The firmware builds with `-Werror` from ESP-IDF; `-Wno-format-truncation` is intentional (`snprintf` truncation into fixed buffers). `-Wshadow` is enabled (as a warning) to catch shadowing bugs.

## Binary formats

Any layout change must be mirrored in the packer, the firmware reader and the README.

**`storage.bin` v2** (`tools/storage-packer/pack_storage.py` ↔ `lib/core/src/storage.c`):
- 64-byte header: `GSTR`, version, VROM offset/size, catalog offset/size, CRC32.
- VROM `GVRO`:
  - 44-byte index entries sorted by name (binary search); names are file basenames without `.bmp`, so they must be unique across subfolders;
  - bitmaps stored row-major, MSB first, 1 = lit.
- Catalog `GCAT`: 176-byte records (items, equipment, skills).
- Validation: the packer fails on duplicates, non-1-bpp files, missing icons and over-long text. `bitmaps/exemple/` is skipped.
- Enum names in the CSVs (`FOOD`, `FIRE`, `SATIETY`, `BURNED`…) are shared with `lib/core/src/types.h`.

**Save state:** 512-byte block with fixed-offset domains + CRC32 (`lib/core/src/save.c`, table in the README), stored in a 16-slot ring in the `save` partition (8 KB, subtype `0x9A`).

**Partitions** (`partitions.csv`):
- `nvs` holds the settings;
- `ota_0`/`ota_1` (1472 KB each);
- `storage` (1 MB, `0x2F0000`, read-only);
- `save` (8 KB, `0x3F0000`).

Flashing firmware or storage never touches `save`/`nvs`.
