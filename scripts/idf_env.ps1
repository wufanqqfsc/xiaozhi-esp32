# ESP-IDF environment for xiaozhi-esp32 (ESP32-S3 / Xtensa).
# Usage: . .\scripts\idf_env.ps1

$env:IDF_PATH = "C:\workspace\tools\esp-idf"
$env:IDF_TOOLS_PATH = "C:\Users\sfan\.espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\sfan\.espressif\python_env\idf5.5_py3.12_env"

$pydir = "$env:IDF_PYTHON_ENV_PATH\Scripts"
$xtensa = "C:\Users\sfan\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
$ninja = "C:\Users\sfan\.espressif\tools\ninja\1.12.1"
$cmake = "C:\Users\sfan\.espressif\tools\cmake\3.30.2\bin"
$idfexe = "C:\Users\sfan\.espressif\tools\idf-exe\1.0.3"
$ulp = "C:\Users\sfan\.espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin"

$env:PATH = "$pydir;$xtensa;$ulp;$ninja;$cmake;$idfexe;$env:IDF_PATH\tools;$env:PATH"

Write-Host "ESP-IDF env ready. IDF_PATH=$env:IDF_PATH"
