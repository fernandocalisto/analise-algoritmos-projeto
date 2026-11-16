param(
    [switch]$SkipBenchmarkRefresh
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$Gpp = Get-Command g++ -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $Gpp) {
    $LocalGpp = Join-Path $env:TEMP "w64devkit\w64devkit\bin\g++.exe"
    if (Test-Path $LocalGpp) {
        $Gpp = $LocalGpp
    } else {
        throw "g++ não encontrado no PATH nem em $LocalGpp"
    }
}

Write-Host "Usando compilador: $Gpp"

New-Item -ItemType Directory -Force -Path "dist" | Out-Null
New-Item -ItemType Directory -Force -Path "data" | Out-Null

function Invoke-Build {
    python scripts\embed_assets.py
    if ($LASTEXITCODE -ne 0) { throw "Falha ao gerar include/web_ui_assets.hpp" }

    & $Gpp -std=c++20 -O3 -Wall -Wextra -Iinclude `
        src\main.cpp src\benchmark_runner.cpp `
        -o dist\AlgoLab.exe `
        -static -static-libgcc -static-libstdc++ `
        -lws2_32 -lshell32 -luser32

    if ($LASTEXITCODE -ne 0) { throw "Falha na compilação C++20" }
}

$JsonPath = Join-Path $Root "data\benchmark_results.json"
$NeedsInitialBench = (-not (Test-Path $JsonPath)) -or (-not $SkipBenchmarkRefresh)

if (-not (Test-Path $JsonPath)) {
    Write-Host "[1/3] Compilando motor C++20 inicial..."
    Invoke-Build
    Write-Host "[2/3] Executando bateria empírica para gerar data/benchmark_results.json..."
    & dist\AlgoLab.exe --cli --benchmark-json data\benchmark_results.json
    if ($LASTEXITCODE -ne 0) { throw "Falha ao executar benchmarks C++" }
    Write-Host "[3/3] Recompilando dist\AlgoLab.exe com snapshot empírico embutido..."
    Invoke-Build
} elseif (-not $SkipBenchmarkRefresh) {
    Write-Host "[1/2] Compilando e atualizando benchmarks reais..."
    Invoke-Build
    & dist\AlgoLab.exe --cli --benchmark-json data\benchmark_results.json
    if ($LASTEXITCODE -ne 0) { throw "Falha ao executar benchmarks C++" }
    Write-Host "[2/2] Recompilando dist\AlgoLab.exe final..."
    Invoke-Build
} else {
    Write-Host "Compilando dist\AlgoLab.exe com dados existentes..."
    Invoke-Build
}

Get-Item dist\AlgoLab.exe | Select-Object Name, Length, LastWriteTime
