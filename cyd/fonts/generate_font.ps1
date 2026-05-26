# Euro-Preisfont neu erzeugen (Node.js/npx erforderlich)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $PSScriptRoot

if (-not (Test-Path "Montserrat-Medium.ttf")) {
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/JulietaUla/Montserrat/master/fonts/ttf/Montserrat-Medium.ttf" -OutFile "Montserrat-Medium.ttf"
}

npx --yes lv_font_conv `
  --font Montserrat-Medium.ttf `
  --size 28 --bpp 4 --format lvgl --no-compress `
  --symbols "0123456789. €*" `
  -o lv_font_price_28.c `
  --lv-font-name lv_font_price_28

# Arduino LVGL: #include <lvgl.h> statt lvgl/lvgl.h
(Get-Content "lv_font_price_28.c" -Raw) -replace '(?s)#ifdef LV_LVGL_H_INCLUDE_SIMPLE.*?#endif', '#include <lvgl.h>' | Set-Content "lv_font_price_28.c" -NoNewline

Copy-Item "lv_font_price_28.c" (Join-Path $root "lv_font_price_28.c") -Force
Copy-Item "lv_font_price_28.h" (Join-Path $root "lv_font_price_28.h") -Force
Write-Host "Fertig: fonts/ und Projektroot aktualisiert."
