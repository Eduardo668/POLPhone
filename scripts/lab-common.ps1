Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

function Invoke-POLPhoneLabWsl {
    param(
        [Parameter(Mandatory = $true)][string]$ScriptName,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][ref]$ExitCode
    )

    $ExitCode.Value = 1

    $wslCommand = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if (-not $wslCommand) {
        Write-Host "WSL não está disponível. Use o laboratório dentro do WSL ou habilite wsl.exe." -ForegroundColor Red
        $ExitCode.Value = 127
        return
    }

    $repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
    $distribution = $null
    $wslRepoRoot = $null

    if ($repoRoot -match '^\\\\wsl(?:\.localhost|\$)\\([^\\]+)\\(.+)$') {
        $distribution = $Matches[1]
        $wslRepoRoot = "/" + ($Matches[2] -replace '\\', '/')
    } else {
        $converted = & $wslCommand.Source --exec wslpath -a -u $repoRoot
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace(($converted | Out-String))) {
            Write-Host "Não foi possível converter o caminho do projeto para o WSL: $repoRoot" -ForegroundColor Red
            $ExitCode.Value = 1
            return
        }
        $wslRepoRoot = (($converted | Select-Object -Last 1) | Out-String).Trim()
    }

    $bashScript = "$wslRepoRoot/scripts/$ScriptName"
    $wslArguments = @()
    if ($distribution) {
        $wslArguments += @("--distribution", $distribution)
    }
    $wslArguments += @("--exec", "bash", $bashScript)
    $wslArguments += $Arguments

    & $wslCommand.Source @wslArguments
    $ExitCode.Value = $LASTEXITCODE
}
