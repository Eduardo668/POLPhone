[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "Both")]
    [string]$Config = "Debug",
    [switch]$Clean,
    [switch]$Tests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)

function Resolve-CMakePath {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswherePath = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswherePath -PathType Leaf) {
        $installation = (& $vswherePath -latest -products * -version "[17.8,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
        if ($installation) {
            $bundledCmake = Join-Path $installation "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $bundledCmake -PathType Leaf) {
                return $bundledCmake
            }
        }
    }
    throw "CMake 3.21+ não encontrado no PATH nem na instalação do Visual Studio. Rode scripts/verify-env.ps1."
}

$cmakePath = Resolve-CMakePath

function Invoke-CMake {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    Write-Host "> $cmakePath $($Arguments -join ' ')"
    & $cmakePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake falhou com exit code $LASTEXITCODE."
    }
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "O build do POLPhone deve ser executado no Windows x64 com Visual Studio 2022."
}
if ($Clean) {
    & (Join-Path $PSScriptRoot "clean.ps1")
}

$testsValue = if ($Tests) { "ON" } else { "OFF" }
Push-Location $repoRoot
try {
    Invoke-CMake -Arguments @("--preset", "windows-x64", "-DPOLPHONE_BUILD_TESTS=$testsValue")
    $buildPresets = switch ($Config) {
        "Debug" { @("build-debug") }
        "Release" { @("build-release") }
        default { @("build-debug", "build-release") }
    }
    foreach ($preset in $buildPresets) {
        Invoke-CMake -Arguments @("--build", "--preset", $preset)
    }
} finally {
    Pop-Location
}

Write-Host "Build $Config concluído."
exit 0
