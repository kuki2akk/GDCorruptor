# Post-build скрипт: вызывается CMake после сборки .geode.
# 1) копирует свежий .geode в папку модов Geode;
# 2) запускает Geometry Dash (если не запрещено и игра ещё не запущена).

param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$ModName = "GD_Corruptor"
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Msg) {
    Write-Host "[postbuild] $Msg"
}

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
    # 3) Папки вида "<root>\Geometry Dash vX" и "<root>\Games\Geometry Dash vX"
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
    throw "GeometryDash.exe not found. Set CORRUPTOR_GD_PATH to the game exe path."
}

# --- Поиск игры и папки модов ---
$GDExe   = Find-GeometryDash
$ModsDir = Join-Path (Split-Path $GDExe) "geode\mods"
Write-Step "GD: $GDExe"

# 1. Найти свежий .geode (имя = id мода из mod.json, обычно != имени проекта)
$Geode = Get-ChildItem -Path $BuildDir -Recurse -Filter "$ModName.geode" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $Geode) {
    # fallback: любой свежий .geode в каталоге сборки
    $Geode = Get-ChildItem -Path $BuildDir -Recurse -Filter "*.geode" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

if (-not $Geode) {
    Write-Step "WARNING: $ModName.geode not found in $BuildDir"
    exit 0
}

# 2. Копирование в папку модов Geode
if (-not (Test-Path $ModsDir)) {
    Write-Step "ERROR: Geode mods folder not found: $ModsDir"
    exit 1
}
Copy-Item -Path $Geode.FullName -Destination $ModsDir -Force
Write-Step "copied $($Geode.Name) -> $ModsDir"

# 3. Запуск игры
if ($env:CORRUPTOR_NO_LAUNCH -eq "1") {
    Write-Step "launch skipped (CORRUPTOR_NO_LAUNCH=1)"
    exit 0
}
$targetRunning = $false
foreach ($p in (Get-Process -Name "GeometryDash" -ErrorAction SilentlyContinue)) {
    try {
        if ($p.MainModule.FileName -eq $GDExe) { $targetRunning = $true; break }
    } catch { }
}
if ($targetRunning) {
    Write-Step "target Geometry Dash already running - skip launch"
    exit 0
}
Start-Process -FilePath $GDExe
Write-Step "launched Geometry Dash"
