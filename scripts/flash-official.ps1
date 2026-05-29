param(
    [string]$Port = "COM7",
    [ValidateSet("userdemo", "uiflow2", "userdemo_se", "boxdemo")]
    [string]$Demo = "userdemo",
    [string]$Firmware = "",
    [int]$Baud = 921600,
    [switch]$EraseFirst,
    [string]$Python = "E:\Anaconda\python.exe"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$OfficialDir = Join-Path $Root "firmware\official"

$DemoMap = @{
    "userdemo"    = "CoreS3_UserDemo_v0.11__f45337edb1966f4cc7d4f78745db921d.bin"
    "uiflow2"     = "UIFlow2.0_CoreS3_v2.4.5__eda11862122c6912baa15e213730ecf8.bin"
    "userdemo_se" = "CoreS3SE_UserDemo_v0.6__fed93812db65b5347b706d1f237021b9.bin"
    "boxdemo"     = "CoreS3_Box_Demo_v0.1__de972cae6c679d8a864dbfefe9193d17.bin"
}

if ([string]::IsNullOrWhiteSpace($Firmware)) {
    $Firmware = Join-Path $OfficialDir $DemoMap[$Demo]
}

if (!(Test-Path $Firmware)) {
    throw "Firmware not found: $Firmware"
}

if (!(Test-Path $Python)) {
    $Python = "python"
}

if ($EraseFirst) {
    & $Python -m esptool --chip esp32s3 --port $Port --baud $Baud erase-flash
}

& $Python -m esptool --chip esp32s3 --port $Port --baud $Baud write-flash --flash-mode dio --flash-freq 80m --flash-size keep 0x0 $Firmware

