$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
$version = [regex]::Match($projectText, 'project\(Anyvoc VERSION ([0-9.]+)').Groups[1].Value
if (!$version) { throw 'Project version not found.' }
$bundle = Join-Path $projectRoot 'build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3'
$standalone = Join-Path $projectRoot 'build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe'
$destination = Join-Path $projectRoot "dist/Anyvoc-$version-Windows"
if (!(Test-Path -LiteralPath $bundle) -or !(Test-Path -LiteralPath $standalone)) { throw 'Build Release first.' }
$module = Join-Path $bundle 'Contents/x86_64-win/Anyvoc.vst3'
foreach ($binary in @($standalone, $module)) {
    if ((Get-Item -LiteralPath $binary).VersionInfo.FileVersion -ne $version) {
        throw "Binary version does not match $version. Reconfigure and rebuild Release: $binary"
    }
}
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -LiteralPath $bundle -Destination $destination -Recurse -Force
$packagedStandalone = Join-Path $destination 'Anyvoc.exe'
if (!(Test-Path -LiteralPath $packagedStandalone) -or
    (Get-FileHash -LiteralPath $standalone).Hash -ne (Get-FileHash -LiteralPath $packagedStandalone).Hash) {
    Copy-Item -LiteralPath $standalone -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/Install-Anyvoc.ps1') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs') -Destination $destination -Recurse -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/快速开始.txt') -Destination $destination -Force
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-Demo.wav')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-Demo.wav') -Destination $destination -Force
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.2-Demo.wav')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.2-Demo.wav') -Destination $destination -Force
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.6-Warp-Demo.wav')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.6-Warp-Demo.wav') -Destination $destination -Force
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.8-Voicing-Demo.wav')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.8-Voicing-Demo.wav') -Destination $destination -Force
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.12-Crossmod-Demo.wav')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.12-Crossmod-Demo.wav') -Destination $destination -Force
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.13-Matrix-Demo.wav')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.13-Matrix-Demo.wav') -Destination $destination -Force
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.14-Sources-Demo.wav')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.14-Sources-Demo.wav') -Destination $destination -Force
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.15-Envelopes-Demo.wav')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist/Anyvoc-v0.15-Envelopes-Demo.wav') -Destination $destination -Force
}
$motionDemo = Join-Path $projectRoot 'dist/Anyvoc-v0.16-Lfo-Motion-Demo.wav'
if (!(Test-Path -LiteralPath $motionDemo)) { throw 'Render the LFO motion demo before packaging.' }
Copy-Item -LiteralPath $motionDemo -Destination $destination -Force
$chaosDemo = Join-Path $projectRoot 'dist/Anyvoc-v0.17-Chaos-Demo.wav'
if (!(Test-Path -LiteralPath $chaosDemo)) { throw 'Render the Chaos demo before packaging.' }
Copy-Item -LiteralPath $chaosDemo -Destination $destination -Force
if ($version -in @('0.19.0', '0.20.0', '0.20.1')) {
    $macroDemo = Join-Path $projectRoot 'dist/Anyvoc-v0.19-Macro-Demo.wav'
    if (!(Test-Path -LiteralPath $macroDemo)) { throw 'Render the Macro demo before packaging.' }
    Copy-Item -LiteralPath $macroDemo -Destination $destination -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $destination 'Licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/JUCE/LICENSE.md') -Destination (Join-Path $destination 'Licenses/JUCE-LICENSE.md') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt') -Destination (Join-Path $destination 'Licenses/VST3-LICENSE.txt') -Force
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath (Join-Path $projectRoot "dist/Anyvoc-$version-Windows-x64.zip") -Force
Write-Host "Packaged: $destination"
