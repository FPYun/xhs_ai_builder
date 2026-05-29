param(
    [string]$Sketch = ".\arduino_demos\01_hello_cores3\01_hello_cores3.ino",
    [string]$Port = "COM7",
    [string]$Fqbn = "m5stack:esp32:m5stack_cores3",
    [string]$ConfigFile = "C:\tmp\m5_arduino_config\arduino-cli.yaml",
    [string]$BuildRoot = "C:\tmp\m5_arduino_build",
    [switch]$Upload,
    [string]$ArduinoCli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
    [switch]$EspSr,
    [string]$EspSrRoot = "C:\tmp\esp-sr-master",
    [string]$DlFftRoot = "C:\tmp\dl_fft\dl_fft",
    [string]$Python = "E:\Anaconda\python.exe",
    [string]$Sketchbook = "C:\tmp\m5_arduino_sketchbook"
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

function Install-EspSrArduinoAdapter {
    param(
        [string]$SourceRoot,
        [string]$SketchbookRoot
    )

    $LibPath = Join-Path $SketchbookRoot "libraries\ESP_SR_Arduino"
    $SrcPath = Join-Path $LibPath "src"
    $LibBinPath = Join-Path $SrcPath "esp32s3"
    New-Item -ItemType Directory -Force -Path $SrcPath, $LibBinPath | Out-Null

    Copy-Item -Force -Path (Join-Path $SourceRoot "include\esp32s3\*.h") -Destination $SrcPath
    Copy-Item -Force -Path (Join-Path $SourceRoot "src\include\*.h") -Destination $SrcPath
    Copy-Item -Force -Path (Join-Path $SourceRoot "src\*.c") -Destination $SrcPath
    Copy-Item -Force -Path (Join-Path $SourceRoot "lib\esp32s3\*.a") -Destination $LibBinPath

    if (Test-Path $DlFftRoot) {
        $DlBase = Join-Path $SrcPath "base"
        $DlIsa = Join-Path $DlBase "isa"
        $DlIsaS3 = Join-Path $DlIsa "esp32s3"
        New-Item -ItemType Directory -Force -Path $DlBase, $DlIsa, $DlIsaS3 | Out-Null
        Copy-Item -Force -Path (Join-Path $DlFftRoot "*.h") -Destination $SrcPath
        Copy-Item -Force -Path (Join-Path $DlFftRoot "*.c") -Destination $SrcPath
        Copy-Item -Force -Path (Join-Path $DlFftRoot "base\*.h") -Destination $DlBase
        Copy-Item -Force -Path (Join-Path $DlFftRoot "base\*.h") -Destination $SrcPath
        Copy-Item -Force -Path (Join-Path $DlFftRoot "base\*.c") -Destination $DlBase
        Copy-Item -Force -Path (Join-Path $DlFftRoot "base\isa\*.h") -Destination $DlIsa
        Copy-Item -Force -Path (Join-Path $DlFftRoot "base\isa\*.h") -Destination $DlBase
        Copy-Item -Force -Path (Join-Path $DlFftRoot "base\isa\*.h") -Destination $SrcPath
        Copy-Item -Force -Path (Join-Path $DlFftRoot "base\isa\esp32s3\*.S") -Destination $DlIsaS3
    }

@"
name=ESP_SR_Arduino
version=2.0.0
author=Espressif
maintainer=local
sentence=ESP-SR precompiled adapter for ESP32-S3.
paragraph=Local adapter for ESP-SR command recognition in Arduino builds.
category=Signal Input/Output
architectures=esp32
precompiled=true
ldflags=-lmultinet -lc_speech_features -lesp_audio_processor -lesp_audio_front_end -lwakenet -lvadnet -lnsnet -ldl_lib -lhufzip -lfst -lflite_g2p -lesp_audio_processor
"@ | Set-Content -Encoding ASCII -Path (Join-Path $LibPath "library.properties")
}

function New-EspSrModelImage {
    param(
        [string]$SourceRoot,
        [string]$BuildPath,
        [string]$PythonExe
    )

    if (!(Test-Path $PythonExe)) {
        throw "Python not found: $PythonExe"
    }

    $SdkConfig = Join-Path $BuildPath "sdkconfig.sr"
@"
CONFIG_SR_MN_CN_MULTINET5_RECOGNITION_QUANT8=y
"@ | Set-Content -Encoding ASCII -Path $SdkConfig

    & $PythonExe (Join-Path $SourceRoot "model\movemodel.py") -d1 $SdkConfig -d2 $SourceRoot -d3 $BuildPath
    $Generated = Join-Path $BuildPath "srmodels\srmodels.bin"
    if (!(Test-Path $Generated)) {
        throw "ESP-SR model image was not generated: $Generated"
    }
    Copy-Item -Force -Path $Generated -Destination (Join-Path $BuildPath "srmodels.bin")
}

if ($EspSr) {
    if (!(Test-Path $EspSrRoot)) {
        throw "ESP-SR root not found: $EspSrRoot. Download https://github.com/espressif/esp-sr to this path first."
    }
    if ($Fqbn -eq "m5stack:esp32:m5stack_cores3") {
        $Fqbn = "${Fqbn}:PartitionScheme=esp_sr_16"
    }
    Install-EspSrArduinoAdapter -SourceRoot $EspSrRoot -SketchbookRoot $Sketchbook
    New-EspSrModelImage -SourceRoot $EspSrRoot -BuildPath $BuildPath -PythonExe $Python
}

$CompileArgs = @("compile", "--fqbn", $Fqbn, "--build-path", $BuildPath)
if ($EspSr) {
    $CompileArgs += @("--build-property", "compiler.cpp.extra_flags=-DCORES3_ENABLE_ESP_SR")
}
$CompileArgs += $Sketch
& $ArduinoCli @CliArgs @CompileArgs

if ($Upload) {
    & $ArduinoCli @CliArgs upload -p $Port --fqbn $Fqbn --input-dir $BuildPath $Sketch
}
