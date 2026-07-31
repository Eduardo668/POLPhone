[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "Both")]
    [string]$Config = "Both",

    [switch]$Clean,

    [ValidateRange(1, 64)]
    [int]$MaxCpuCount = 1,

    [string]$WindowsSdkVersion = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

$expectedTag = "2.17"
$expectedCommit = "5a457451fa2712ba18e12b01738e8ff3af2b26fd"
$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
$pjprojectRoot = Join-Path $repoRoot "third_party\pjproject"
$solutionPath = Join-Path $pjprojectRoot "pjproject-vs14.sln"
$configTemplate = Join-Path $repoRoot "cmake\config_site.h.in"
$configDestination = Join-Path $pjprojectRoot "pjlib\include\pj\config_site.h"
$buildLogDirectory = Join-Path $repoRoot "logs\build"

$useWslGit = $repoRoot.StartsWith("\\wsl.", [StringComparison]::OrdinalIgnoreCase) -or
    $repoRoot.StartsWith("\\wsl$", [StringComparison]::OrdinalIgnoreCase)
$wslDistro = ""
$wslRepoRoot = ""
if ($useWslGit) {
    $uncParts = $repoRoot.TrimStart('\').Split('\')
    if ($uncParts.Count -lt 3) {
        throw "Não foi possível interpretar o caminho WSL: $repoRoot"
    }
    $wslDistro = $uncParts[1]
    $wslRepoRoot = "/" + (($uncParts[2..($uncParts.Count - 1)]) -join "/")
}

$expectedLibraries = @(
    "pjsua2-lib",
    "pjsua-lib",
    "pjsip-ua",
    "pjsip-simple",
    "pjsip-core",
    "pjmedia-codec",
    "pjmedia",
    "pjmedia-audiodev",
    "pjmedia-videodev",
    "pjnath",
    "pjlib-util",
    "libsrtp",
    "libresample",
    "libgsmcodec",
    "libspeex",
    "libilbccodec",
    "libg7221codec",
    "libyuv",
    "libwebrtc",
    "libbaseclasses",
    "pjlib"
)

$requiredLibraryProjects = @(
    "pjlib",
    "pjlib_util",
    "pjnath",
    "libsrtp",
    "libresample",
    "libgsmcodec",
    "libspeex",
    "libilbccodec",
    "libg7221codec",
    "libyuv",
    "libwebrtc",
    "libbaseclasses",
    "pjmedia",
    "pjmedia_audiodev",
    "pjmedia_videodev",
    "pjmedia_codec",
    "pjsip_core",
    "pjsip_simple",
    "pjsip_ua",
    "pjsua_lib",
    "pjsua2_lib"
)

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Write-Host "> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Comando falhou com exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Invoke-RepoGit {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    if ($useWslGit) {
        Invoke-Native -FilePath "wsl.exe" -Arguments (@("-d", $wslDistro, "--", "git", "-C", $wslRepoRoot) + $Arguments)
    } else {
        Invoke-Native -FilePath "git" -Arguments (@("-C", $repoRoot) + $Arguments)
    }
}

function Invoke-PjprojectGitOutput {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    if ($useWslGit) {
        $output = & wsl.exe -d $wslDistro -- git -C "$wslRepoRoot/third_party/pjproject" @Arguments 2>&1
    } else {
        $output = & git -C $pjprojectRoot @Arguments 2>&1
    }
    if ($LASTEXITCODE -ne 0) {
        throw "git no pjproject falhou: $($output -join [Environment]::NewLine)"
    }
    return (($output | Out-String).Trim())
}

function Find-VisualStudio {
    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswherePath = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswherePath -PathType Leaf)) {
        throw "vswhere.exe não encontrado. Instale o Visual Studio 2022 17.8+ com Desktop C++."
    }

    $installation = (& $vswherePath -latest -products * -version "[17.8,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if (-not $installation) {
        throw "Visual Studio 2022 17.8+ com o workload Desktop C++ não encontrado."
    }

    $msbuild = Join-Path $installation "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild -PathType Leaf)) {
        throw "MSBuild não encontrado em: $msbuild"
    }
    return $msbuild
}

function Resolve-WindowsSdkVersion {
    param([string]$RequestedVersion)

    $kitsKey = "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots"
    if (-not (Test-Path $kitsKey)) {
        throw "Windows 10 SDK não encontrado no registro."
    }

    $kitsRoot = (Get-ItemProperty $kitsKey).KitsRoot10
    $includeRoot = Join-Path $kitsRoot "Include"
    if ($RequestedVersion) {
        $requestedPath = Join-Path $includeRoot $RequestedVersion
        if (-not (Test-Path (Join-Path $requestedPath "um\Windows.h") -PathType Leaf)) {
            throw "Windows SDK solicitado não encontrado: $RequestedVersion"
        }
        if ([version]$RequestedVersion -lt [version]"10.0.19041.0") {
            throw "Windows SDK $RequestedVersion é inferior ao mínimo 10.0.19041.0."
        }
        return $RequestedVersion
    }

    $candidates = @(
        Get-ChildItem $includeRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            try {
                if (Test-Path (Join-Path $_.FullName "um\Windows.h") -PathType Leaf) {
                    [pscustomobject]@{ Name = $_.Name; Version = [version]$_.Name }
                }
            } catch {
                # Ignora diretórios que não representam versões do SDK.
            }
        }
    )
    $selected = $candidates | Where-Object { $_.Version -ge [version]"10.0.19041.0" } | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $selected) {
        throw "Nenhum Windows SDK compatível (mínimo 10.0.19041.0) foi encontrado."
    }
    return $selected.Name
}

function Assert-PjprojectRevision {
    $actualCommit = Invoke-PjprojectGitOutput -Arguments @("rev-parse", "HEAD")
    $actualTag = Invoke-PjprojectGitOutput -Arguments @("describe", "--tags", "--exact-match")
    if (($actualCommit -ne $expectedCommit) -or ($actualTag -ne $expectedTag)) {
        throw "pjproject incorreto: tag=$actualTag commit=$actualCommit; esperado tag=$expectedTag commit=$expectedCommit."
    }
}

function Assert-RuntimePropertySheets {
    $debugProps = Join-Path $pjprojectRoot "build\vs\pjproject-vs14-debug-dynamic-defaults.props"
    $releaseProps = Join-Path $pjprojectRoot "build\vs\pjproject-vs14-release-dynamic-defaults.props"
    if (-not (Select-String -Path $debugProps -SimpleMatch "<RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>" -Quiet)) {
        throw "Debug-Dynamic não declara /MDd (MultiThreadedDebugDLL) na tag fixada."
    }
    if (-not (Select-String -Path $releaseProps -SimpleMatch "<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>" -Quiet)) {
        throw "Release-Dynamic não declara /MD (MultiThreadedDLL) na tag fixada."
    }
}

function Get-PjprojectLibraries {
    $libraryDirectories = @(Get-ChildItem -Path $pjprojectRoot -Directory -Recurse | Where-Object { $_.Name -eq "lib" })
    return @($libraryDirectories | ForEach-Object { Get-ChildItem -Path $_.FullName -File -Filter "*.lib" -ErrorAction SilentlyContinue })
}

function Assert-ExpectedLibraries {
    param([Parameter(Mandatory = $true)][string]$PjConfiguration)

    $allLibraries = @(Get-PjprojectLibraries)
    $resolved = [System.Collections.Generic.List[string]]::new()
    foreach ($baseName in $expectedLibraries) {
        $pattern = "^{0}-x86_64-x64-vc[^-]+-{1}\.lib$" -f [regex]::Escape($baseName), [regex]::Escape($PjConfiguration)
        $candidates = @($allLibraries | Where-Object { $_.Name -match $pattern })
        if ($candidates.Count -eq 0) {
            throw "Biblioteca ausente para ${PjConfiguration}: ${baseName}. Rode novamente o setup e confira o log do MSBuild."
        }
        if ($candidates.Count -gt 1) {
            throw "Mais de uma biblioteca corresponde a ${baseName}/${PjConfiguration}: $($candidates.FullName -join ', ')"
        }
        $resolved.Add($candidates[0].FullName)
    }

    Write-Host "Bibliotecas validadas para ${PjConfiguration}: $($resolved.Count)"
    $resolved | ForEach-Object { Write-Host " - $_" }
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "Este script compila com a solução oficial do Visual Studio e deve ser executado no Windows."
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git não encontrado no PATH."
}
if (-not (Test-Path $configTemplate -PathType Leaf)) {
    throw "Template ausente: $configTemplate"
}

Invoke-RepoGit -Arguments @("submodule", "update", "--init", "--recursive")
if (-not (Test-Path $solutionPath -PathType Leaf)) {
    throw "Solução oficial não encontrada: $solutionPath"
}
Assert-PjprojectRevision
Assert-RuntimePropertySheets

$templateHash = (Get-FileHash $configTemplate -Algorithm SHA256).Hash
$destinationHash = if (Test-Path $configDestination -PathType Leaf) {
    (Get-FileHash $configDestination -Algorithm SHA256).Hash
} else {
    ""
}
if ($templateHash -ne $destinationHash) {
    Copy-Item -Path $configTemplate -Destination $configDestination -Force
    $destinationHash = (Get-FileHash $configDestination -Algorithm SHA256).Hash
}
if ($templateHash -ne $destinationHash) {
    throw "A cópia de config_site.h não corresponde ao template versionado."
}
Write-Host "config_site.h gerado a partir do template versionado."

if (-not (Test-Path $buildLogDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $buildLogDirectory -Force | Out-Null
}

$msbuildPath = Find-VisualStudio
$sdkVersion = Resolve-WindowsSdkVersion -RequestedVersion $WindowsSdkVersion
Write-Host "MSBuild: $msbuildPath"
Write-Host "Windows SDK: $sdkVersion"
Write-Host "Toolset: v143"

$configurations = switch ($Config) {
    "Debug" { @("Debug-Dynamic") }
    "Release" { @("Release-Dynamic") }
    default { @("Debug-Dynamic", "Release-Dynamic") }
}

foreach ($pjConfiguration in $configurations) {
    $buildLogPath = Join-Path $buildLogDirectory "pjproject-$pjConfiguration.log"
    $buildTargets = $requiredLibraryProjects -join ";"
    $cleanTargets = ($requiredLibraryProjects | ForEach-Object { "${_}:Clean" }) -join ";"
    $commonArguments = @(
        $solutionPath,
        "/nologo",
        "/m:$MaxCpuCount",
        "/v:minimal",
        "/p:Configuration=$pjConfiguration",
        "/p:Platform=x64",
        "/p:BuildToolset=v143",
        "/p:PlatformToolset=v143",
        "/p:WindowsTargetPlatformVersion=$sdkVersion",
        "/fl",
        "/flp:LogFile=$buildLogPath;Verbosity=normal;Encoding=UTF-8"
    )

    if ($Clean) {
        Invoke-Native -FilePath $msbuildPath -Arguments ($commonArguments + "/t:$cleanTargets")
    }
    Invoke-Native -FilePath $msbuildPath -Arguments ($commonArguments + "/t:$buildTargets")
    if (-not (Test-Path $buildLogPath -PathType Leaf)) {
        throw "MSBuild terminou sem criar o log esperado: $buildLogPath"
    }
    Write-Host "Log MSBuild: $buildLogPath"
    Assert-ExpectedLibraries -PjConfiguration $pjConfiguration
}

$submoduleStatus = Invoke-PjprojectGitOutput -Arguments @("status", "--porcelain")
if ($submoduleStatus) {
    throw "O build modificou arquivos rastreados do pjproject:`n$submoduleStatus"
}

Write-Host "pjproject $expectedTag ($expectedCommit) preparado com sucesso."
Write-Host "Runtime: Debug-Dynamic=/MDd; Release-Dynamic=/MD."
exit 0
