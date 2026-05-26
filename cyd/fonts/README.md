# Schrift mit Euro-Zeichen (€)

Die Standard-LVGL-Fonts (Montserrat) enthalten kein **€** – deshalb gibt es hier eine eigene Preis-Schrift.

## Dateien

| Datei | Zweck |
|-------|--------|
| `lv_font_price_28.c` | LVGL-Font (28 px): Ziffern, `.`, Leerzeichen, `€`, `*` |
| `lv_font_price_28.h` | Deklaration für den Sketch |
| `Montserrat-Medium.ttf` | Quell-Font zum Neu-Generieren |

## Arduino IDE (anderer PC)

Die IDE kompiliert **nur** `.c`/`.cpp` im **Sketch-Ordner** (gleiche Ebene wie die `.ino`), nicht Unterordner wie `fonts/`.

**So übernehmen:**

1. Den `cyd/`-Ordner kopieren und in der Arduino IDE öffnen (`cyd.ino`).
2. Diese Dateien müssen **neben `cyd.ino`** liegen (liegen auch im `cyd/`-Ordner):
   - `lv_font_price_28.c`
   - `lv_font_price_28.h`
3. `User_Setup.h` und `lv_conf.h` **im gleichen Ordner lassen** – `cyd.ino` bindet sie ein (nicht nach `Arduino/libraries` kopieren).

Der Sketch nutzt `#include "lv_font_price_28.h"` und `&lv_font_price_28` für die Preiszeile (mit **€**).

Der Ordner `fonts/` enthält dieselben Dateien plus TTF und `generate_font.ps1` zum Neu-Erzeugen.

## Font neu erzeugen (optional)

Auf einem PC mit Node.js, im Ordner `cyd/`:

```powershell
cd cyd
npx --yes lv_font_conv --font fonts/Montserrat-Medium.ttf --size 28 --bpp 4 --format lvgl --no-compress --symbols "0123456789. €*" -o fonts/lv_font_price_28.c --lv-font-name lv_font_price_28
```

Danach `lv_font_price_28.c` und `.h` aus `fonts/` nach `cyd/` kopieren (neben `cyd.ino`).
