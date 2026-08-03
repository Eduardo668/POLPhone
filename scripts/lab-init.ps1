[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$ShowSecrets
)

. (Join-Path $PSScriptRoot "lab-common.ps1")
$context = Get-POLPhoneLabContext
$examplePath = Join-Path $context.LabDir ".env.example"

Assert-POLPhoneDocker
if (-not (Test-Path -LiteralPath $examplePath -PathType Leaf)) {
    throw "Template ausente: $examplePath"
}

if (Test-Path -LiteralPath $context.EnvFile) {
    if (-not $Force) {
        $confirmation = Read-Host "O .env já existe. Digite SOBRESCREVER para recriá-lo"
        if ($confirmation -cne "SOBRESCREVER") {
            Write-Host "Configuração preservada; nenhuma alteração realizada."
            exit 0
        }
    }
}

function New-LabSecret {
    $bytes = New-Object byte[] 32
    $generator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $generator.GetBytes($bytes)
    } finally {
        $generator.Dispose()
    }
    return ([BitConverter]::ToString($bytes).Replace("-", "").ToLowerInvariant())
}

$secret1001 = New-LabSecret
$secret1002 = New-LabSecret
$secret2001 = New-LabSecret
$content = [IO.File]::ReadAllText($examplePath)
$content = $content.Replace("LAB_SIP_1001_SECRET=GENERATE_WITH_LAB_INIT", "LAB_SIP_1001_SECRET=$secret1001")
$content = $content.Replace("LAB_SIP_1002_SECRET=GENERATE_WITH_LAB_INIT", "LAB_SIP_1002_SECRET=$secret1002")
$content = $content.Replace("LAB_PJSIP_2001_SECRET=GENERATE_WITH_LAB_INIT", "LAB_PJSIP_2001_SECRET=$secret2001")
[IO.File]::WriteAllText($context.EnvFile, $content, (New-Object Text.UTF8Encoding($false)))

& git -C $context.RepoRoot check-ignore --quiet "lab/asterisk/.env"
if ($LASTEXITCODE -ne 0) {
    throw "Proteção Git inválida: lab/asterisk/.env não está ignorado."
}

Write-Host "Laboratório inicializado sem iniciar containers."
Write-Host "Ramais fictícios chan_sip criados: 1001 e 1002."
Write-Host "Endpoint PJSIP opcional criado: 2001."
Write-Host "Segredos foram gravados somente em lab/asterisk/.env e não foram exibidos."
if ($ShowSecrets) {
    Write-Warning "Exibição local solicitada explicitamente; não copie estes valores para logs ou commits."
    Write-Host "1001: $secret1001"
    Write-Host "1002: $secret1002"
    Write-Host "2001: $secret2001"
}
