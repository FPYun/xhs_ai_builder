param(
    [string]$Sketch = ".\arduino_demos\01_hello_cores3\01_hello_cores3.ino",
    [string]$Port = "COM7",
    [string]$Fqbn = "m5stack:esp32:m5stack_cores3",
    [string]$ConfigFile = "C:\tmp\m5_arduino_config\arduino-cli.yaml",
    [string]$BuildRoot = "C:\tmp\m5_arduino_build",
    [switch]$Upload,
    [string]$ArduinoCli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $ArduinoCli)) {
    throw "arduino-cli not found: $ArduinoCli"
}

if ($ConfigFile -and !(Test-Path $ConfigFile)) {
    throw "Arduino config not found: $ConfigFile"
}

if (!(Test-Path $Sketch)) {
    throw "Sketch not found: $Sketch"
}

$CliArgs = @()
if ($ConfigFile) {
    $CliArgs += @("--config-file", $ConfigFile)
}

$SketchName = [IO.Path]::GetFileNameWithoutExtension($Sketch)
$BuildPath = Join-Path $BuildRoot $SketchName
New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null

& $ArduinoCli @CliArgs compile --fqbn $Fqbn --build-path $BuildPath $Sketch

if ($Upload) {
    & $ArduinoCli @CliArgs upload -p $Port --fqbn $Fqbn --input-dir $BuildPath $Sketch
}

