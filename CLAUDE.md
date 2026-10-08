# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

Guará is a virtual-pet / RPG handheld built on an ESP32 (NodeMCU-32S) with a 128x64 1-bit OLED (SH1106 I2C), buzzer, MicroSD and 6 buttons. Firmware v0.1.0 implements every menu in the README (see the phase status in `docs/PLANO.md`).

- `README.md` — the design spec (in Portuguese) and the source of truth: hardware pinout, partitions, save-state layout, UI standards, menus, combat formulas, life cycle, sprites, Lua API.
- `docs/GAME_DESIGN.md` — numbers and behaviours the README leaves open (decay rates, XP curve, stage levels, extra combat formulas, unspecified controls). Values live in `lib/nucleo/src/regras.h`; keep both in sync.
- `docs/PLANO.md` — phase plan, hardware decisions (D1–D10) and the README fix backlog (H/I/L/A items). Mark items there when you resolve them.
- `catalog/*.csv` — packer input for items, equipment and skills. **Empty on purpose:** the author fills the content in `CATALOG.md`; never invent catalog entries. Tests use `test/fixtures/catalog/`. `bitmaps/` — 1-bit VROM assets; `exemplos/sdcard/` — sample SD content (Lua game).

All docs, comments, identifiers, UI text and tool output are in Portuguese; keep new content consistent with that.

**Side convention:** whenever the README says "direita"/"esquerda", it means the **device's** side — the device's right is the user's left (e.g. the clock "no canto superior direito" is drawn at the viewer's top-left). Pixel coordinates are plain screen coordinates (X=0 at the viewer's left). **Exception:** the d-pad buttons are named by screen direction from the user's view (button "Direita", GPIO27, moves +X). In code, `TECLA_ESQ`/`TECLA_DIR` are screen directions; the GPIO mapping is in `src/hal/pinos.h`. Check `bitmaps/exemple/*.bmp` mockups when in doubt.

## Commands

`pio` is not on PATH; use `~/.platformio/penv/bin/pio`. The board is on `/dev/ttyACM0` (CH343 adapter).

```bash
pio run -e esp32                          # build firmware (must stay warning-free)
pio run -e esp32 -t upload                # flash bootloader + partition table + app
pio run -e esp32 -t upload_storage        # pack bitmaps/ + catalog/ and flash only the storage partition
pio run -e esp32 -t build_storage         # just generate .pio/storage.bin
pio test -e native                        # all host tests (Unity)
pio test -e native -f test_nucleo         # one test folder (test_crc32, test_gfx, test_nucleo, test_midi_ini)
python3 tools/font-builder/build_font.py  # regenerate lib/gfx/src/fonte_5x8.c from fonte_5x8.txt
python3 tools/bmp-builder/bmp_builder.py in.png -o bitmaps/icons/x.bmp --preset icon --method otsu -p
```

`bmp_builder.py` converts PNG/JPG/SVG/… to the canonical 1-bit BMP. Its presets are icon 16x16, sprite 32x32, mannequin 32x56, boss 64x64 and screen 128x64; `-p` prints a preview. Image loading falls back GdkPixbuf → Pillow → ImageMagick. A C++ port builds with `make` in `tools/bmp-builder`.

`test_nucleo` reads `.pio/storage_teste.bin`, generated automatically from `test/fixtures/catalog/` by the `native` pre-script `tools/pio_storage_teste.py`.

**Serial debug console** (`tools/console.py`, run with `~/.platformio/penv/bin/python`): opening the port **resets the board** on this adapter, so use one `roteiro` session per test: `console.py roteiro "espera 4" "teclas jsk" "tela out.png" "estado"`. Keys: `w s a d` = d-pad (screen directions), `j` = A, `k` = B; `P` dumps the framebuffer, `S` prints the pet state. Never open the port with DTR asserted (GPIO0 low → download mode). The user may be pressing physical buttons at the same time.

## Code layout

- `src/` — ESP-IDF main component (hardware/IDF code only). `CMakeLists.txt` lists the `REQUIRES`; add new IDF components there.
  - `hal/` drivers: `display` (SH1106, async task sending only changed pages), `botoes` (event queue), `audio` (LEDC task: effects + background music source), `bateria`, `sdcard`, `relogio` (DS3231/NTP/TZ), `energia` (deep sleep), `wifi`, `i2c`, `pinos.h`.
  - `sistema/` services: `app` (boot, main loop, per-minute life cycle, inactivity sleep, battery emergency), `persist` (storage mmap, save ring, RTC-RAM mirror), `config` (NVS), `rede` (Wi-Fi on demand: never call `wifi_*` directly from screens — use `rede_usar/rede_aguardar/rede_liberar`, `rede_executar` for background jobs, `rede_suspender` around memory-hungry tasks), `alarmes`, `ota`, `depuracao`.
  - `ui/` — screen stack (`ui.h`), standard components (`componentes.h`), modals + virtual keyboard (`modal.h`).
  - `telas/` — one file per menu; `telas.h` lists them. `submenu.h` builds static standard sub-menus; `sprite.h` composes head + body + eyes.
- `lib/` — pure C11, **no IDF includes**, compiled for both `esp32` and `native`: `crc32`, `gfx` (page-format framebuffer, primitives, UTF-8 text, draw modes, BMP decoder), `nucleo` (types, storage/VROM/catalog reader, save format + ring, pet life cycle, combat), `midi` (SMF → monophonic), `ini`, `lua` (vendored 5.4.7, `LUA_32BITS`, no io/os/package/debug; excluded from `native`).

UI patterns:
- A screen is a `tela_t` with callbacks. Reusable screens that can be stacked (modals, `submenu`) keep per-layer state indexed by `ui_camada_atual()`.
- Modal results arrive through a callback *after* the modal has been popped, so the callback may open new screens.
- Call `app_alterou()` after changing `g_guara`. It mirrors to RTC RAM and sets the dirty flag; the flash save happens when returning to the menu or main screen. Use `app_salvar()` for critical events.

The firmware builds with `-Werror` from ESP-IDF; `-Wno-format-truncation` is intentional (`snprintf` truncation into fixed buffers).

## Binary formats

Any layout change must be mirrored in the packer, the firmware reader and the README.

**`storage.bin` v2** (`tools/storage-packer/pack_storage.py` ↔ `lib/nucleo/src/storage.c`):
- 64-byte header: `GSTR`, version, VROM offset/size, catalog offset/size, CRC32.
- VROM `GVRO`:
  - 44-byte index entries sorted by name (binary search); names are file basenames without `.bmp`, so they must be unique across subfolders;
  - bitmaps stored row-major, MSB first, 1 = lit.
- Catalog `GCAT`: 176-byte records (items, equipment, skills).
- Validation: the packer fails on duplicates, non-1-bpp files, missing icons and over-long text. `bitmaps/exemple/` is skipped.
- Enums are shared with `lib/nucleo/src/tipos.h`.

**Save state:** 512-byte block with fixed-offset domains + CRC32 (`lib/nucleo/src/save.c`, table in the README), stored in a 16-slot ring in the `save` partition (8 KB, subtype `0x9A`).

**Partitions** (`partitions.csv`):
- `nvs` holds the settings;
- `ota_0`/`ota_1` (1472 KB each);
- `storage` (1 MB, `0x2F0000`, read-only);
- `save` (8 KB, `0x3F0000`).

Flashing firmware or storage never touches `save`/`nvs`.

## Versioning

SemVer `MAJOR.MINOR.PATCH` (`-DGUARA_VERSAO` in `platformio.ini`, history in `CHANGELOG.md`), bumped **only** for functional changes to firmware or storage. Documentation-only changes never bump the version. **Rule for 0.x.x builds:** the whole catalog is unlocked — every item at 255 units plus every equipment and skill owned — applied at birth and on every boot (`guara_versao_desenvolvimento` / `guara_liberar_catalogo`).
