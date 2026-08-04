[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "Both")]
    [string]$Config = "Debug",
    [switch]$SkipCore
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
$project = Join-Path $repoRoot "gui\POLPhone.Gui.vcxproj"

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "A interface WinUI 3 deve ser compilada no Windows x64."
}

$programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere -PathType Leaf)) {
    throw "vswhere.exe não encontrado. Instale Visual Studio 2022 17.8 ou superior."
}
$installation = (& $vswhere -latest -products * -version "[17.8,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installation) {
    throw "Visual Studio 2022 com 'Desenvolvimento para desktop com C++' não encontrado."
}
$winUiComponentId = "Microsoft.VisualStudio.ComponentGroup.WindowsAppDevelopment.VC.BuildTools"
$winUiInstallation = (& $vswhere -latest -products * -version "[17.8,18.0)" `
    -requires $winUiComponentId `
    -property installationPath | Select-Object -First 1)
$appxPackageRoot = Join-Path $installation "MSBuild\Microsoft\VisualStudio\v17.0\AppxPackage"
$appxTargets = Join-Path $appxPackageRoot "Microsoft.AppxPackage.Targets"
$priTasks = Join-Path $appxPackageRoot "Microsoft.Build.Packaging.Pri.Tasks.dll"
$winUiArtifactsPresent = (Test-Path $appxTargets -PathType Leaf) `
    -and (Test-Path $priTasks -PathType Leaf)
if (-not $winUiInstallation -and -not $winUiArtifactsPresent) {
    throw "Componente WinUI ausente. Instale 'C++ WinUI app development tools' (ID $winUiComponentId). Também não foram encontrados $appxTargets e $priTasks."
}
$msbuild = Join-Path $installation "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild -PathType Leaf)) { throw "MSBuild não encontrado: $msbuild" }

$configs = switch ($Config) {
    "Debug" { @("Debug") }
    "Release" { @("Release") }
    default { @("Debug", "Release") }
}

foreach ($current in $configs) {
    if (-not $SkipCore) {
        & (Join-Path $PSScriptRoot "build.ps1") -Config $current
        if ($LASTEXITCODE -ne 0) { throw "Build do motor $current falhou." }
    }
    Write-Host "> $msbuild $project /restore /t:Build /p:Configuration=$current /p:Platform=x64"
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        if ($repoRoot.StartsWith("\\")) {
            # mt.exe não processa corretamente o prefixo de caminho estendido gerado
            # pelo linker para um projeto aberto diretamente por UNC (caso WSL).
            # pushd cria uma unidade temporária e popd a remove ao final do comando.
            $commandLine = 'pushd "' + $repoRoot + '" && "' + $msbuild +
                '" "gui\POLPhone.Gui.vcxproj" /restore /t:Build /m' +
                ' /p:Configuration=' + $current + ' /p:Platform=x64 && popd'
            $buildOutput = @(& cmd.exe /d /s /c $commandLine 2>&1)
        } else {
            $buildOutput = @(& $msbuild $project /restore /t:Build /m `
                /p:Configuration=$current /p:Platform=x64 2>&1)
        }
        $buildExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $buildOutput | ForEach-Object { Write-Host $_ }
    if ($buildExitCode -ne 0) {
        $buildLog = ($buildOutput | ForEach-Object { $_.ToString() }) -join "`n"
        $firstError = $buildOutput |
            ForEach-Object { $_.ToString().Trim() } |
            Where-Object {
                $_ -match '(?i)(:\s*(fatal\s+)?error\s+[A-Z]+\d+|:\s*erro\s+[A-Z]+\d+|error\s+NU\d+|erro\s+NU\d+)'
            } |
            Select-Object -First 1
        if (-not $firstError) {
            $firstError = $buildOutput |
                ForEach-Object { $_.ToString().Trim() } |
                Where-Object { $_ -match '(?i)\b(error|erro|failed|falhou)\b' } |
                Select-Object -First 1
        }
        if (-not $firstError) { $firstError = "consulte a saída completa acima" }

        $missingWorkload = $buildLog -match '(?i)(MSB4019|Microsoft\.AppxPackage\.Targets|Microsoft\.Build\.Packaging\.Pri\.Tasks\.dll|XamlCompiler\.exe.*(not found|não encontrado))'
        if ($missingWorkload) {
            throw "Build WinUI $current falhou. Primeiro erro: $firstError. O log indica componente WinUI ausente; instale 'C++ WinUI app development tools' (ID $winUiComponentId)."
        }
        throw "Build WinUI $current falhou. Primeiro erro: $firstError. O ambiente WinUI já foi validado; corrija esse erro antes de repetir o build."
    }
}

Write-Host "Build da interface $Config concluído."
exit 0
