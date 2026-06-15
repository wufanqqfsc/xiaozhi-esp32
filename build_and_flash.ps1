# Build and flash xiaozhi-esp32 on Windows.
#
# Usage:
#   .\build_and_flash.ps1
#   .\build_and_flash.ps1 -Port COM9
#   .\build_and_flash.ps1 -Board waveshare/esp32-s3-touch-lcd-1.85b
#   .\build_and_flash.ps1 -BuildOnly
#   .\build_and_flash.ps1 -Monitor

param(
    [string]$Board = "waveshare/esp32-s3-touch-lcd-1.85b",
    [string]$BoardName = "",
    [string]$Port = "",
    [switch]$Monitor,
    [switch]$BuildOnly,
    [switch]$SetTarget
)

$ErrorActionPreference = "Stop"

function Write-Info($msg)  { Write-Host "[INFO] $msg" -ForegroundColor Cyan }
function Write-Ok($msg)    { Write-Host "[SUCCESS] $msg" -ForegroundColor Green }
function Write-Err($msg)   { Write-Host "[ERROR] $msg" -ForegroundColor Red }

function Find-Esp32Port {
    $devices = Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match 'VID_303A' }

    foreach ($dev in $devices) {
        if ($dev.FriendlyName -match '\((COM\d+)\)') {
            return $Matches[1]
        }
    }
    return $null
}

function Get-BoardConfig {
    param([string]$BoardPath)

    $cfgFile = Join-Path $BoardPath "config.json"
    if (-not (Test-Path $cfgFile)) {
        throw "Board config not found: $cfgFile"
    }

    $cfg = Get-Content $cfgFile -Raw | ConvertFrom-Json
    $build = $cfg.builds[0]
    if (-not $build) {
        throw "No builds defined in $cfgFile"
    }

  return [PSCustomObject]@{
        Target    = $cfg.target
        Name      = $build.name
        BoardPath = $BoardPath
    }
}

function Resolve-BoardConfigSymbol {
    param([string]$BoardLeaf)

    $cmake = Get-Content (Join-Path $PSScriptRoot "main\CMakeLists.txt")
    $pattern = "set(BOARD_TYPE `"$BoardLeaf`")"

    for ($i = 0; $i -lt $cmake.Count; $i++) {
        if ($cmake[$i] -notmatch [regex]::Escape($pattern)) {
            continue
        }
        for ($j = $i - 1; $j -ge 0; $j--) {
            if ($cmake[$j] -match 'if\((CONFIG_BOARD_TYPE_[^)]+)\)') {
                return $Matches[1]
            }
        }
    }

    throw "Cannot resolve CONFIG_BOARD_TYPE for board leaf: $BoardLeaf"
}

function Ensure-BoardSdkconfig {
    param(
        [string]$SdkconfigPath,
        [string]$BoardConfigSymbol
    )

    $line = "$BoardConfigSymbol=y"
    $content = Get-Content $SdkconfigPath -ErrorAction SilentlyContinue
    if ($content -match "(?m)^$([regex]::Escape($BoardConfigSymbol))=y$") {
        return
    }

    Write-Info "Append $line to sdkconfig"
    Add-Content -Path $SdkconfigPath -Value ""
    Add-Content -Path $SdkconfigPath -Value "# Append by build_and_flash.ps1"
    Add-Content -Path $SdkconfigPath -Value $line
}

$root = $PSScriptRoot
$envScript = Join-Path $root "scripts\idf_env.ps1"

if (-not (Test-Path $envScript)) {
    Write-Err "ESP-IDF env script not found: $envScript"
    exit 1
}

. $envScript

$python = Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe"
$idfPy = Join-Path $env:IDF_PATH "tools\idf.py"

if (-not (Test-Path $python)) {
    Write-Err "Python not found at $python"
    exit 1
}

if (-not (Test-Path $idfPy)) {
    Write-Err "idf.py not found at $idfPy"
    exit 1
}

Set-Location $root

$boardPath = Join-Path $root "main\boards\$($Board -replace '/', '\')"
$boardCfg = Get-BoardConfig -BoardPath $boardPath
$boardLeaf = $Board.Split('/')[-1]
if (-not $BoardName) {
    $BoardName = $boardCfg.Name
}
$boardConfigSymbol = Resolve-BoardConfigSymbol -BoardLeaf $boardLeaf
$sdkconfigPath = Join-Path $root "sdkconfig"

Write-Info "Board: $Board"
Write-Info "Board name: $BoardName"
Write-Info "Target: $($boardCfg.Target)"
Write-Info "Kconfig: $boardConfigSymbol"

if ($SetTarget -or -not (Test-Path $sdkconfigPath)) {
    Write-Info "Setting target $($boardCfg.Target)..."
    & $python $idfPy set-target $boardCfg.Target
    if ($LASTEXITCODE -ne 0) {
        Write-Err "set-target failed!"
        exit $LASTEXITCODE
    }
}

Ensure-BoardSdkconfig -SdkconfigPath $sdkconfigPath -BoardConfigSymbol $boardConfigSymbol

Write-Info "Building xiaozhi..."
& $python $idfPy "-DBOARD_NAME=$BoardName" "-DBOARD_TYPE=$Board" build
if ($LASTEXITCODE -ne 0) {
    Write-Err "Build failed!"
    exit $LASTEXITCODE
}
Write-Ok "Build completed!"

if ($BuildOnly) {
    exit 0
}

if (-not $Port) {
    $Port = Find-Esp32Port
}

if (-not $Port) {
    Write-Err "No ESP32 device found (VID_303A)."
    Write-Info "Connect the board via USB, or pass -Port COMx"
    exit 1
}

Write-Ok "Found device: $Port"

Write-Info "Flashing firmware to $Port..."
& $python $idfPy -p $Port flash
if ($LASTEXITCODE -ne 0) {
    Write-Err "Flash failed!"
    exit $LASTEXITCODE
}
Write-Ok "Flash completed successfully!"

if ($Monitor) {
    Write-Info "Starting monitor (Ctrl+] to exit)..."
    & $python $idfPy -p $Port monitor
    exit $LASTEXITCODE
}
