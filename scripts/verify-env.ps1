[CmdletBinding()]
param([switch]$Gui)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

$expectedTag = "2.17"
$expectedCommit = "5a457451fa2712ba18e12b01738e8ff3af2b26fd"
$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
$pjprojectRoot = Join-Path $repoRoot "third_party\pjproject"
$failures = [System.Collections.Generic.List[string]]::new()

function Write-Check {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Passed,
        [Parameter(Mandatory = $true)][string]$Detail
    )

    $label = if ($Passed) { "OK" } else { "FALHA" }
    Write-Host ("[{0,-5}] {1}: {2}" -f $label, $Name, $Detail)
    if (-not $Passed) {
        $failures.Add("${Name}: ${Detail}")
    }
}

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $output = & git @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') falhou: $($output -join [Environment]::NewLine)"
    }
    return (($output | Out-String).Trim())
}

Write-Host "POLPhone - verificação do ambiente Windows x64"
Write-Host "Raiz: $repoRoot"
Write-Host ""

$isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
Write-Check "Windows" $isWindowsHost "Windows 10/11 x64 é obrigatório para compilar"

$gitCommand = Get-Command git -ErrorAction SilentlyContinue
Write-Check "Git" ($null -ne $gitCommand) $(if ($gitCommand) { (& git --version) } else { "não encontrado no PATH" })

$vsInstallation = $null
$msbuildPath = $null
$toolsetDetail = "indisponível fora do Windows"
if ($isWindowsHost) {
    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswherePath = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswherePath -PathType Leaf) {
        $vsInstallation = (& $vswherePath -latest -products * -version "[17.8,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    }

    if ($vsInstallation) {
        $msbuildPath = Join-Path $vsInstallation "MSBuild\Current\Bin\MSBuild.exe"
        $toolsetVersionFile = Join-Path $vsInstallation "VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
        if (Test-Path $toolsetVersionFile -PathType Leaf) {
            $toolsetVersion = (Get-Content $toolsetVersionFile -Raw).Trim()
            $clPath = Join-Path $vsInstallation "VC\Tools\MSVC\$toolsetVersion\bin\Hostx64\x64\cl.exe"
            $toolsetPassed = (Test-Path $clPath -PathType Leaf) -and $toolsetVersion.StartsWith("14.")
            $toolsetDetail = "v143/MSVC $toolsetVersion; cl.exe x64: $clPath"
        } else {
            $toolsetPassed = $false
            $toolsetDetail = "arquivo de versão do MSVC não encontrado"
        }
    } else {
        $toolsetPassed = $false
    }
} else {
    $toolsetPassed = $false
}
Write-Check "Visual Studio 2022" ($null -ne $vsInstallation) $(if ($vsInstallation) { $vsInstallation } else { "17.8+ com Desktop C++ não encontrado" })
Write-Check "MSBuild" (($null -ne $msbuildPath) -and (Test-Path $msbuildPath -PathType Leaf)) $(if ($msbuildPath) { $msbuildPath } else { "não encontrado" })
Write-Check "Toolset v143" $toolsetPassed $toolsetDetail

$winUiToolsPassed = $false
$winUiToolsDetail = "indisponível fora do Windows"
if ($isWindowsHost -and $vsInstallation) {
    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswherePath = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    $winUiComponentId = "Microsoft.VisualStudio.ComponentGroup.WindowsAppDevelopment.VC.BuildTools"
    $winUiInstallation = (& $vswherePath -latest -products * -version "[17.8,18.0)" `
        -requires $winUiComponentId `
        -property installationPath | Select-Object -First 1)
    $appxPackageRoot = Join-Path $vsInstallation "MSBuild\Microsoft\VisualStudio\v17.0\AppxPackage"
    $appxTargets = Join-Path $appxPackageRoot "Microsoft.AppxPackage.Targets"
    $priTasks = Join-Path $appxPackageRoot "Microsoft.Build.Packaging.Pri.Tasks.dll"
    $winUiArtifactsPresent = (Test-Path $appxTargets -PathType Leaf) `
        -and (Test-Path $priTasks -PathType Leaf)
    $winUiToolsPassed = ($null -ne $winUiInstallation) -or $winUiArtifactsPresent
    $winUiToolsDetail = if ($null -ne $winUiInstallation) {
        "componente $winUiComponentId confirmado pelo vswhere"
    } elseif ($winUiArtifactsPresent) {
        "componente confirmado pelos artefatos Microsoft.AppxPackage.Targets e Microsoft.Build.Packaging.Pri.Tasks.dll"
    } else {
        "instale o componente $winUiComponentId; ausentes: $appxTargets e/ou $priTasks"
    }
}
if ($Gui) {
    Write-Check "WinUI 3 C++" $winUiToolsPassed $winUiToolsDetail
} else {
    $winUiLabel = if ($winUiToolsPassed) { "OK" } else { "INFO" }
    Write-Host ("[{0,-5}] WinUI 3 C++: {1} (use -Gui para tornar obrigatório)" -f `
        $winUiLabel, $winUiToolsDetail)
}

$cmakePath = $null
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} elseif ($vsInstallation) {
    $bundledCmake = Join-Path $vsInstallation "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $bundledCmake -PathType Leaf) {
        $cmakePath = $bundledCmake
    }
}
$cmakePassed = $false
$cmakeDetail = "não encontrado no PATH nem na instalação do Visual Studio"
if ($cmakePath) {
    $cmakeOutput = @(& $cmakePath --version)
    $cmakeExitCode = $LASTEXITCODE
    $cmakeFirstLine = ($cmakeOutput | Select-Object -First 1)
    $versionMatch = [regex]::Match([string]$cmakeFirstLine, "[0-9]+\.[0-9]+\.[0-9]+")
    if ($cmakeExitCode -eq 0 -and $versionMatch.Success) {
        $cmakeVersion = [version]$versionMatch.Value
        $cmakePassed = $cmakeVersion -ge [version]"3.21.0"
        $cmakeDetail = "$cmakeVersion em $cmakePath (mínimo 3.21.0)"
    } else {
        $cmakeDetail = "não foi possível interpretar: $cmakeFirstLine"
    }
}
Write-Check "CMake" $cmakePassed $cmakeDetail

$sdkPassed = $false
$sdkDetail = "indisponível fora do Windows"
if ($isWindowsHost) {
    $kitsKey = "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots"
    if (Test-Path $kitsKey) {
        $kitsRoot = (Get-ItemProperty $kitsKey).KitsRoot10
        $includeRoot = Join-Path $kitsRoot "Include"
        $sdkCandidates = @(
            Get-ChildItem $includeRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                try {
                    [pscustomobject]@{ Name = $_.Name; Version = [version]$_.Name }
                } catch {
                    # Ignora diretórios que não representam versões do SDK.
                }
            }
        )
        $selectedSdk = $sdkCandidates | Sort-Object Version -Descending | Select-Object -First 1
        if ($selectedSdk) {
            $sdkPassed = $selectedSdk.Version -ge [version]"10.0.19041.0"
            $preference = if ($selectedSdk.Version -ge [version]"10.0.22621.0") { "padrão atendido" } else { "compatível, abaixo do padrão 10.0.22621.0" }
            $sdkDetail = "$($selectedSdk.Name) ($preference)"
        } else {
            $sdkDetail = "nenhum Windows 10 SDK encontrado"
        }
    } else {
        $sdkDetail = "registro Windows Kits não encontrado"
    }
}
Write-Check "Windows SDK" $sdkPassed $sdkDetail

$submodulePopulated = (Test-Path (Join-Path $pjprojectRoot ".git")) -and (Test-Path (Join-Path $pjprojectRoot "pjproject-vs14.sln"))
Write-Check "Submodule pjproject" $submodulePopulated $(if ($submodulePopulated) { $pjprojectRoot } else { "rode: git submodule update --init --recursive" })

$solutionPath = Join-Path $pjprojectRoot "pjproject-vs14.sln"
if ($submodulePopulated) {
    $solutionText = Get-Content -LiteralPath $solutionPath -Raw
    $hasDebugDynamicX64 = $solutionText.Contains("Debug-Dynamic|x64")
    $hasReleaseDynamicX64 = $solutionText.Contains("Release-Dynamic|x64")
    Write-Check "Solução oficial" $true $solutionPath
    Write-Check "Debug-Dynamic|x64" $hasDebugDynamicX64 "configuração da solução oficial"
    Write-Check "Release-Dynamic|x64" $hasReleaseDynamicX64 "configuração da solução oficial"
}

if ($submodulePopulated -and $gitCommand) {
    try {
        $actualCommit = Invoke-Git -Arguments @("-C", $pjprojectRoot, "rev-parse", "HEAD")
        $actualTag = Invoke-Git -Arguments @("-C", $pjprojectRoot, "describe", "--tags", "--exact-match")
        Write-Check "Tag pjproject" ($actualTag -eq $expectedTag) "$actualTag (esperada: $expectedTag)"
        Write-Check "Commit pjproject" ($actualCommit -eq $expectedCommit) "$actualCommit"
    } catch {
        Write-Check "Versão pjproject" $false $_.Exception.Message
    }
}

$configTemplate = Join-Path $repoRoot "cmake\config_site.h.in"
Write-Check "config_site.h.in" (Test-Path $configTemplate -PathType Leaf) $configTemplate

Write-Host ""
if ($failures.Count -gt 0) {
    Write-Host "Ambiente inválido: $($failures.Count) verificação(ões) falharam." -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Ambiente válido para o build Windows x64 do POLPhone." -ForegroundColor Green
exit 0
