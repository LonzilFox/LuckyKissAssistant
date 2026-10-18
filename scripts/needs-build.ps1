param(
    [Parameter(Mandatory=$true)][string]$Root,
    [switch]$Update
)
$ErrorActionPreference = 'Stop'
try {
    $Root = (Resolve-Path -LiteralPath $Root).Path
    $native = Join-Path $Root 'out\dev\LuckyKissMod.dll'
    $managed = Join-Path $Root 'out\dev\LuckyKissTrainer.exe'
    $manifest = Join-Path $Root 'out\dev\.source-hashes'
    $files = @(
        Get-ChildItem -LiteralPath (Join-Path $Root 'src') -Recurse -File |
            Where-Object { $_.Extension -in @('.cpp', '.h', '.hpp') }
        Get-ChildItem -LiteralPath (Join-Path $Root 'ui') -Recurse -File |
            Where-Object { $_.FullName -notmatch '[\\/](bin|obj)[\\/]' -and $_.Extension -in @('.cs', '.xaml', '.csproj') }
        Get-Item -LiteralPath (Join-Path $Root 'run-dev.bat')
        Get-Item -LiteralPath (Join-Path $Root 'scripts\needs-build.ps1')
    ) | Sort-Object FullName
    $lines = foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($Root.Length).TrimStart('\', '/')
        "$relativePath|$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)"
    }
    $signature = $lines -join "`n"
    if ($Update) {
        $dir = Split-Path -Parent $manifest
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
        [System.IO.File]::WriteAllText($manifest, $signature, (New-Object System.Text.UTF8Encoding($false)))
        exit 0
    }
    if (-not (Test-Path -LiteralPath $native) -or -not (Test-Path -LiteralPath $managed) -or -not (Test-Path -LiteralPath $manifest)) { exit 1 }
    $previous = [System.IO.File]::ReadAllText($manifest, [System.Text.Encoding]::UTF8)
    if ($previous -cne $signature) { exit 1 }
    exit 0
} catch {
    Write-Host "[needs-build] $($_.Exception.Message)"
    exit 1
}
