# Tao file UF2 flash duoc cho XIAO nRF52840.
#
# TAI SAO CAN SCRIPT NAY (khong flash thang zephyr.uf2 duoc):
#   Bootloader la MCUboot co verify chu ky. File "build/<app>/zephyr/zephyr.uf2"
#   la ban CHUA KY -> MCUboot tu choi -> board khong boot len app (trieu chung:
#   khong co COM cua app, khong phat BLE, khong log gi vi console bootloader da tat).
#   Phai gop: mcuboot.hex + app DA KY (zephyr.signed.hex) -> uf2conv.
#   >> Day chinh la loi da lam mat nhieu gio debug ngay 24-25/07.
#
# Dung: .\make_uf2.ps1      (chay sau khi "west build" thanh cong)

$ErrorActionPreference = "Stop"

$Toolchain = "C:\ncs\toolchains\dcbdc366a1"
$Py        = "$Toolchain\opt\bin\python.exe"
$Uf2Conv   = "G:\Nrf_SDK\v3.4.0\zephyr\scripts\build\uf2conv.py"
$Proj      = $PSScriptRoot
$Family    = "0xADA52840"                # nRF52840 - Adafruit UF2 family ID
$OutUf2    = "$Proj\tag_Xiao_SIGNED.uf2"

$env:PYTHONPATH = "$Toolchain\opt\bin;$Toolchain\opt\bin\Lib;$Toolchain\opt\bin\Lib\site-packages"

# Ten thu muc domain trong build/ = ten thu muc project (sysbuild dat theo do).
$AppDomain = Split-Path $Proj -Leaf
$AppHex = "$Proj\build\$AppDomain\zephyr\zephyr.signed.hex"
# Truoc khi doi ten project, domain ten la "beacon" -> fallback cho build cu
if (-not (Test-Path $AppHex)) { $AppHex = "$Proj\build\beacon\zephyr\zephyr.signed.hex" }
$McuHex = "$Proj\build\mcuboot\zephyr\zephyr.hex"
$Merged = "$Proj\build\merged.hex"

foreach ($f in @($McuHex, $AppHex)) {
    if (-not (Test-Path $f)) { throw "Khong tim thay: $f  (da chay 'west build' chua?)" }
}

& $Py -c @"
from intelhex import IntelHex
mb  = IntelHex(r'$McuHex')
app = IntelHex(r'$AppHex')
if mb.maxaddr() >= app.minaddr():
    raise SystemExit('LOI: mcuboot va app CHONG LAN dia chi!')
m = IntelHex(); m.merge(mb, overlap='replace'); m.merge(app, overlap='replace')
m.write_hex_file(r'$Merged')
print('  mcuboot   0x%05x - 0x%05x  (mong doi bat dau 0x27000)' % (mb.minaddr(), mb.maxaddr()))
print('  app (ky)  0x%05x - 0x%05x  (mong doi bat dau 0x33000)' % (app.minaddr(), app.maxaddr()))
"@
if ($LASTEXITCODE -ne 0) { throw "Gop hex that bai" }

& $Py $Uf2Conv -c -f $Family -o $OutUf2 $Merged
if ($LASTEXITCODE -ne 0) { throw "uf2conv that bai" }

$size = (Get-Item $OutUf2).Length
Write-Output ""
Write-Output "OK -> $OutUf2  ($size bytes)"
Write-Output ""
Write-Output "Flash: double-tap nut RESET de vao bootloader, roi copy file tren vao drive XIAO-SENSE."
