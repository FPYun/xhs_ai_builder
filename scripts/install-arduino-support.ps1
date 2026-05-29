param(
    [string]$ArduinoCli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
    [string]$M5StackIndex = "https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json",
    [string]$CoreVersion = "2.1.4",
    [string]$Fqbn = "m5stack:esp32:m5stack_cores3",
    [string]$ConfigDir = "C:\tmp\m5_arduino_config",
    [string]$DataDir = "C:\tmp\m5_arduino_data",
    [string]$DownloadsDir = "C:\tmp\m5_arduino_downloads",
    [string]$SketchbookDir = "C:\tmp\m5_arduino_sketchbook"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $ArduinoCli)) {
    throw "arduino-cli not found: $ArduinoCli"
}

$ConfigFile = Join-Path $ConfigDir "arduino-cli.yaml"
New-Item -ItemType Directory -Force -Path $ConfigDir, $DataDir, $DownloadsDir, $SketchbookDir | Out-Null

if (!(Test-Path $ConfigFile)) {
    & $ArduinoCli config init --dest-dir $ConfigDir --overwrite
}

$CliArgs = @("--config-file", $ConfigFile)

& $ArduinoCli @CliArgs config set directories.data $DataDir
& $ArduinoCli @CliArgs config set directories.downloads $DownloadsDir
& $ArduinoCli @CliArgs config set directories.user $SketchbookDir
& $ArduinoCli @CliArgs config set board_manager.additional_urls $M5StackIndex

& $ArduinoCli @CliArgs core update-index
& $ArduinoCli @CliArgs core install "m5stack:esp32@$CoreVersion"
& $ArduinoCli @CliArgs lib install M5Unified
& $ArduinoCli @CliArgs lib install M5GFX
& $ArduinoCli @CliArgs board listall M5CoreS3

Write-Host "Expected FQBN: $Fqbn"
Write-Host "Arduino config: $ConfigFile"
