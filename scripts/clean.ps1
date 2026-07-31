[CmdletBinding()]
param([switch]$All)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
$pjprojectRoot = Join-Path $repoRoot "third_party\pjproject"

function Remove-ProjectDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPrefix = $repoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Recusa de segurança: caminho fora do repositório: $fullPath"
    }
    if (Test-Path $fullPath -PathType Container) {
        Write-Host "Removendo: $fullPath"
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}

Remove-ProjectDirectory -Path (Join-Path $repoRoot "build")
$logsDirectory = Join-Path $repoRoot "logs"
if (Test-Path $logsDirectory -PathType Container) {
    Get-ChildItem -Path $logsDirectory -File -Filter "*.log" | ForEach-Object {
        Write-Host "Removendo log: $($_.FullName)"
        Remove-Item -LiteralPath $_.FullName -Force
    }
}

if ($All) {
    if (-not (Test-Path (Join-Path $pjprojectRoot "pjproject-vs14.sln") -PathType Leaf)) {
        throw "Submodule pjproject não inicializado; limpeza -All cancelada."
    }
    $generatedDirectories = @(
        Get-ChildItem -Path $pjprojectRoot -Directory -Recurse | Where-Object {
            $_.Name -in @("lib", "bin", "output")
        } | Sort-Object { $_.FullName.Length } -Descending
    )
    foreach ($directory in $generatedDirectories) {
        Remove-ProjectDirectory -Path $directory.FullName
    }
}

Write-Host "Limpeza concluída."
exit 0
