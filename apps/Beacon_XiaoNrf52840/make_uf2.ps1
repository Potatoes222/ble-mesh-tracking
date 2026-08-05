# Produce a UF2 file that is flashable on XIAO nRF52840.
#
# WHY THIS SCRIPT IS NEEDED (cannot just flash zephyr.uf2 directly):
#   The bootloader is MCUboot with signature verification. The file
#   "build/<app>/zephyr/zephyr.uf2" is UNSIGNED -> MCUboot rejects it
#   -> the board never boots the app (symptoms: no app COM port, no
#   BLE advertisement, no log because the bootloader's console is
#   disabled).
#   You must merge: mcuboot.hex + the SIGNED app (zephyr.signed.hex)
#   -> then run uf2conv.
#
# Usage: .\make_uf2.ps1      (run after a successful `west build`)

$ErrorActionPreference = "Stop"

# Requires the Zephyr / nRF Connect SDK environment to be activated first
# (so ZEPHYR_BASE is set and `python` + `intelhex` are on PATH). Typical:
#   & "$env:USERPROFILE\ncs\vX.Y.Z\zephyr\zephyr-env.ps1"
if (-not $env:ZEPHYR_BASE) {
    throw "ZEPHYR_BASE not set. Activate your Zephyr environment before running this script."
}

$Py        = "python"
$Uf2Conv   = "$env:ZEPHYR_BASE\scripts\build\uf2conv.py"
$Proj      = $PSScriptRoot
$Family    = "0xADA52840"                # nRF52840 - Adafruit UF2 family ID
$OutUf2    = "$Proj\tag_Xiao_SIGNED.uf2"

# Domain folder under build/ = the project folder name (sysbuild names
# it that way).
$AppDomain = Split-Path $Proj -Leaf
$AppHex = "$Proj\build\$AppDomain\zephyr\zephyr.signed.hex"
# Before the project was renamed the domain was "beacon" -> fallback
# for older build trees.
if (-not (Test-Path $AppHex)) { $AppHex = "$Proj\build\beacon\zephyr\zephyr.signed.hex" }
$McuHex = "$Proj\build\mcuboot\zephyr\zephyr.hex"
$Merged = "$Proj\build\merged.hex"

foreach ($f in @($McuHex, $AppHex)) {
    if (-not (Test-Path $f)) { throw "Not found: $f  (did you run 'west build' first?)" }
}

& $Py -c @"
from intelhex import IntelHex
mb  = IntelHex(r'$McuHex')
app = IntelHex(r'$AppHex')
if mb.maxaddr() >= app.minaddr():
    raise SystemExit('ERROR: mcuboot and app addresses OVERLAP!')
m = IntelHex(); m.merge(mb, overlap='replace'); m.merge(app, overlap='replace')
m.write_hex_file(r'$Merged')
print('  mcuboot   0x%05x - 0x%05x  (expected to start at 0x27000)' % (mb.minaddr(), mb.maxaddr()))
print('  app (signed)  0x%05x - 0x%05x  (expected to start at 0x33000)' % (app.minaddr(), app.maxaddr()))
"@
if ($LASTEXITCODE -ne 0) { throw "hex merge failed" }

& $Py $Uf2Conv -c -f $Family -o $OutUf2 $Merged
if ($LASTEXITCODE -ne 0) { throw "uf2conv failed" }

$size = (Get-Item $OutUf2).Length
Write-Output ""
Write-Output "OK -> $OutUf2  ($size bytes)"
Write-Output ""
Write-Output "Flash: double-tap the RESET button to enter the bootloader, then copy the file above onto the XIAO-SENSE drive."
