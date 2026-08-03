[CmdletBinding()]
param(
    [int]$TimeoutSec = 180,
    [switch]$AllowLanExposure
)

. (Join-Path $PSScriptRoot "lab-common.ps1")
$context = Get-POLPhoneLabContext
Assert-POLPhoneDocker
$envValues = Read-POLPhoneLabEnv -Path $context.EnvFile
Assert-POLPhoneLabEnv -Values $envValues

if ($envValues["LAB_BIND_IP"] -ne "127.0.0.1") {
    Write-Warning "LAB_BIND_IP não é localhost. As portas serão expostas no IP local $($envValues['LAB_BIND_IP'])."
    Write-Warning "Restrinja o firewall ao computador de teste e nunca use uma interface de VPN/VLAN corporativa."
    if (-not $AllowLanExposure) {
        throw "Exposição à LAN bloqueada. Revise os IPs e repita com -AllowLanExposure somente após autorização."
    }
}

Push-Location $context.LabDir
try {
    Invoke-POLPhoneDocker -Arguments @("compose", "config", "--quiet")
    Invoke-POLPhoneDocker -Arguments @("compose", "build")
    Invoke-POLPhoneDocker -Arguments @("compose", "up", "-d")

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
    $asteriskHealth = "starting"
    $gatewayHealth = "starting"
    while ([DateTime]::UtcNow -lt $deadline) {
        $asteriskHealth = ((& docker inspect --format "{{.State.Health.Status}}" $context.ContainerName 2>$null) | Out-String).Trim()
        $asteriskInspectExitCode = $LASTEXITCODE
        $gatewayHealth = ((& docker inspect --format "{{.State.Health.Status}}" $context.GatewayContainerName 2>$null) | Out-String).Trim()
        $gatewayInspectExitCode = $LASTEXITCODE
        if ($asteriskInspectExitCode -eq 0 -and $gatewayInspectExitCode -eq 0 -and
            $asteriskHealth -eq "healthy" -and $gatewayHealth -eq "healthy") { break }
        if ($asteriskHealth -eq "unhealthy" -or $gatewayHealth -eq "unhealthy") { break }
        Start-Sleep -Seconds 2
    }
    if ($asteriskHealth -ne "healthy" -or $gatewayHealth -ne "healthy") {
        & docker compose ps
        & docker compose logs --tail 120 asterisk gateway
        throw "O laboratório não ficou healthy em $TimeoutSec segundos (Asterisk: $asteriskHealth; gateway: $gatewayHealth)."
    }

    Write-Host "Laboratório healthy. Dados sanitizados para o POLPhone:"
    Write-Host "  ramal: 1001"
    Write-Host "  registrar: sip:$($envValues['LAB_ADVERTISED_IP']):$($envValues['LAB_CHAN_SIP_HOST_PORT'])"
    Write-Host "  transporte: UDP"
    Write-Host "  codecs: PCMU/ulaw e PCMA/alaw"
    Write-Host "  senha: consulte LAB_SIP_1001_SECRET localmente no .env (não será exibida)"
    Write-Host "  extensões: 600, 9991, 9992, 9993 e 9999"
} finally {
    Pop-Location
}
