param(
    [string]$Port = "COM7",
    [string]$Python = "E:\Anaconda\python.exe"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $Python)) {
    $Python = "python"
}

& $Python -m esptool --chip esp32s3 --port $Port chip-id

