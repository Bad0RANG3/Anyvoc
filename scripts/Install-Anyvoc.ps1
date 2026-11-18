param([string]$SourceBundle, [switch]$AllUsers)
$ErrorActionPreference = 'Stop'
if (!$SourceBundle) {
    $SourceBundle = Join-Path $PSScriptRoot 'Anyvoc.vst3'
    if (!(Test-Path -LiteralPath $SourceBundle)) {
        $SourceBundle = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist/Anyvoc-Windows/Anyvoc.vst3'
    }
}
if (!(Test-Path -LiteralPath (Join-Path $SourceBundle 'Contents/x86_64-win/Anyvoc.vst3'))) { throw 'Complete Anyvoc.vst3 bundle was not found.' }
$destinationRoot = if ($AllUsers) { Join-Path $env:CommonProgramFiles 'VST3' } else { Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3' }
$destination = Join-Path $destinationRoot 'Anyvoc.vst3'
New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
if (Test-Path -LiteralPath $destination) {
    $backup = Join-Path $destinationRoot ('Anyvoc-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.zip')
    Compress-Archive -LiteralPath $destination -DestinationPath $backup
}
Copy-Item -LiteralPath $SourceBundle -Destination $destinationRoot -Recurse -Force
Write-Host "Installed: $destination"
Write-Host 'FL Studio: Options > Manage plugins > Find installed plugins; then load Anyvoc as an instrument.'
Write-Host "If the host does not scan the user VST3 directory, add this search folder: $destinationRoot"
