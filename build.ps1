# Финальный скрипт сборки GD Corruptor.
#   powershell -ExecutionPolicy Bypass -File build.ps1          # собрать и запустить GD
#   powershell -ExecutionPolicy Bypass -File build.ps1 -NoLaunch # собрать без запуска

param(
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Find-GeometryDash {
    # 1) Явное переопределение через переменную окружения
    if ($env:CORRUPTOR_GD_PATH -and (Test-Path $env:CORRUPTOR_GD_PATH)) {
        return (Resolve-Path $env:CORRUPTOR_GD_PATH).Path
    }
    # 2) Явные известные установки
    $explicit = @(
        "D:\gd 2.2081\GeometryDash.exe",
        "D:\Games\Geometry Dash v2.2074\GeometryDash.exe"
    )
    foreach ($c in $explicit) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    # 3) Типичные пути Steam
    $steamCandidates = @(
        "D:\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe",
        "C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\GeometryDash.exe",
        "C:\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe"
    )
    $steamPath = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    if ($steamPath) {
        $steamCandidates += (Join-Path $steamPath "steamapps\common\Geometry Dash\GeometryDash.exe")
    }
    foreach ($c in $steamCandidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    # 3) Папки вида "D:\Games\Geometry Dash vX" / "C:\...\Geometry Dash vX"
    foreach ($root in @("D:\", "C:\")) {
        $dirs = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "Geometry Dash*" }
        foreach ($d in $dirs) {
            $exe = Join-Path $d.FullName "GeometryDash.exe"
            if (Test-Path $exe) { return (Resolve-Path $exe).Path }
        }
        # второй уровень вложенности (D:\Games\..., C:\Games\...)
        $parents = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue
        foreach ($p in $parents) {
            $sub = Get-ChildItem -Path $p.FullName -Directory -Filter "Geometry Dash*" -ErrorAction SilentlyContinue
            foreach ($d in $sub) {
                $exe = Join-Path $d.FullName "GeometryDash.exe"
                if (Test-Path $exe) { return (Resolve-Path $exe).Path }
            }
        }
    }
    # 4) Путь из профиля geode CLI
    $geodeExe = (Get-Command geode -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if ($geodeExe) {
        $profile = (& $geodeExe profile list 2>$null) -join "`n"
        if ($profile -match 'GeometryDash\.exe') {
            $m = [regex]::Match($profile, '[A-Za-z]:\\[^"|\r\n]*GeometryDash\.exe')
            if ($m.Success -and (Test-Path $m.Value)) { return (Resolve-Path $m.Value).Path }
        }
    }
    throw "GeometryDash.exe not found. Set CORRUPTOR_GD_PATH to the game exe path."
}

# --- Поиск игры и папки модов ---
$GDExe   = Find-GeometryDash
$ModsDir = Join-Path (Split-Path $GDExe) "geode\mods"
Write-Host "[build] GD: $GDExe"

if (-not (Test-Path $ModsDir)) {
    throw "Geode mods folder not found: $ModsDir. Install Geode first (https://geode-sdk.org)."
}

# --- Поиск geode CLI ---
$LocalCli = Join-Path $Root "tools\geode\geode.exe"
$Cli = if (Test-Path $LocalCli) { $LocalCli }
       else { (Get-Command geode -ErrorAction SilentlyContinue | Select-Object -First 1).Source }
if (-not $Cli) { throw "geode CLI not found. Install: winget install GeodeSDK.GeodeCLI" }
Write-Host "[build] geode CLI: $Cli"

# --- Geode SDK v5 (для GD 2.2081). Перекрываем устаревшие переменные окружения:
#     в системе остались GEODE_SDK/GEODE_BINDINGS_REPO_PATH под старый SDK 4.10.2.
#     Переопределить можно через CORRUPTOR_GEODE_SDK / CORRUPTOR_BINDINGS. ---
if ($env:CORRUPTOR_GEODE_SDK -and (Test-Path (Join-Path $env:CORRUPTOR_GEODE_SDK "CMakeLists.txt"))) {
    $env:GEODE_SDK = $env:CORRUPTOR_GEODE_SDK
}
else {
    $env:GEODE_SDK = "D:\GeodeSDK5"
    if (-not (Test-Path (Join-Path $env:GEODE_SDK "CMakeLists.txt"))) {
        throw "Geode SDK v5 not found at D:\GeodeSDK5 (clone https://github.com/geode-sdk/sdk there)"
    }
}
Write-Host "[build] GEODE_SDK: $env:GEODE_SDK"

if ($env:CORRUPTOR_BINDINGS -and (Test-Path (Join-Path $env:CORRUPTOR_BINDINGS "CMakeLists.txt"))) {
    $env:GEODE_BINDINGS_REPO_PATH = $env:CORRUPTOR_BINDINGS
}
else {
    $env:GEODE_BINDINGS_REPO_PATH = "D:\GeodeBindings"
    if (-not (Test-Path (Join-Path $env:GEODE_BINDINGS_REPO_PATH "CMakeLists.txt"))) {
        throw "Bindings repo not found at D:\GeodeBindings (clone https://github.com/geode-sdk/bindings there)"
    }
}
Write-Host "[build] GEODE_BINDINGS_REPO_PATH: $env:GEODE_BINDINGS_REPO_PATH"

# --- Сборка (CLI сам ставит мод в папку модов) ---
# Post-build не запускает игру: стартуем её один раз сами, после полной сборки.
$env:CORRUPTOR_NO_LAUNCH = "1"

Push-Location $Root
try {
    & $Cli build --config Release
    if ($LASTEXITCODE -ne 0) { throw "geode build failed (exit code $LASTEXITCODE)" }
}
finally {
    Pop-Location
}

# --- Страховочная копия свежего .geode ---
$Geode = Get-ChildItem -Path $Root -Recurse -Filter "*.geode" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($Geode) {
    Copy-Item -Path $Geode.FullName -Destination $ModsDir -Force
    Write-Host "[build] installed: $($Geode.Name) -> $ModsDir"
}
else {
    Write-Host "[build] WARNING: no .geode produced"
}

# --- Запуск игры ---
if ($NoLaunch) {
    Write-Host "[build] launch skipped (-NoLaunch)"
}
else {
    # Запущена ли именно целевая версия игры (могла остаться открытой старая)
    $targetRunning = $false
    foreach ($p in (Get-Process -Name "GeometryDash" -ErrorAction SilentlyContinue)) {
        try {
            if ($p.MainModule.FileName -eq $GDExe) { $targetRunning = $true; break }
        } catch { }
    }
    if ($targetRunning) {
        Write-Host "[build] target Geometry Dash already running - skip launch"
    }
    else {
        Start-Process -FilePath $GDExe
        Write-Host "[build] launched Geometry Dash"
    }
}
