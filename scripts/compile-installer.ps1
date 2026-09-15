[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceDir,
    [Parameter(Mandatory = $true)]
    [string] $Version,
    [Parameter(Mandatory = $true)]
    [string] $OutputDir,
    [string] $IssFile,
    [string] $GitHubRepo = 'JanitorPuppo/remote-deck-obs-plugin'
)

$ErrorActionPreference = 'Stop'

function Get-ISCC {
    foreach ($candidate in @(
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
        )) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Install-InnoSetup {
    $setup = Join-Path $env:TEMP 'innosetup-6.exe'
    Invoke-WebRequest -Uri 'https://files.jrsoftware.org/is/6/innosetup-6.0.3.exe' -OutFile $setup
    Start-Process -FilePath $setup -ArgumentList '/VERYSILENT', '/SP-', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait
}

$iscc = Get-ISCC
if (-not $iscc) {
    Write-Host 'Inno Setup not found. Installing...'
    Install-InnoSetup
    $iscc = Get-ISCC
}
if (-not $iscc) {
    throw 'Could not find ISCC.exe after installing Inno Setup.'
}

if (-not $IssFile) {
    $IssFile = Join-Path $PSScriptRoot '..\cmake\windows\resources\installer.iss'
}

$SourceDir = (Resolve-Path $SourceDir).Path
$OutputDir = (Resolve-Path $OutputDir).Path
$IssFile = (Resolve-Path $IssFile).Path
$outputName = "obs-remote-deck-$Version-windows-installer"

& $iscc /Qp `
    "/DMyAppVersion=$Version" `
    "/DSourceDir=$SourceDir" `
    "/DOutputDir=$OutputDir" `
    "/DOutputBaseFilename=$outputName" `
    $IssFile

if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed with exit code $LASTEXITCODE"
}

$installerPath = Join-Path $OutputDir "$outputName.exe"
$sha256 = (Get-FileHash -Algorithm SHA256 -Path $installerPath).Hash.ToLowerInvariant()
$installerUrl = "https://github.com/$GitHubRepo/releases/download/$Version/$outputName.exe"
$notesUrl = "https://github.com/$GitHubRepo/releases/tag/$Version"

$manifest = [ordered]@{
    version  = $Version
    notesUrl = $notesUrl
    windows  = [ordered]@{
        installerUrl = $installerUrl
        sha256       = $sha256
    }
}

$manifestPath = Join-Path $OutputDir 'latest.json'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Path $manifestPath -Encoding utf8

Write-Host "Wrote $installerPath"
Write-Host "Wrote $manifestPath (sha256=$sha256)"
