$ErrorActionPreference = 'Stop'
$cacheRenderer = (Resolve-Path 'build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe').Path
$cacheRenders = @(
 @{ Flag='--demo'; Label='legacy'; Output='Anyvoc-v0.20.3-Legacy.wav' },
 @{ Flag='--lfo-motion-demo'; Label='motion'; Output='Anyvoc-v0.20.3-Motion.wav' },
 @{ Flag='--chaos-demo'; Label='chaos'; Output='Anyvoc-v0.20.3-Chaos.wav' },
 @{ Flag='--macro-demo'; Label='macro'; Output='Anyvoc-v0.20.3-Macro.wav' }
)
$cacheResults = @()
foreach ($cacheRender in $cacheRenders) {
 $cacheOutput = Join-Path $PSScriptRoot $cacheRender.Output
 $cacheLog = Join-Path $PSScriptRoot ('render-' + $cacheRender.Label + '.txt')
 if ((Test-Path -LiteralPath $cacheOutput) -or (Test-Path -LiteralPath $cacheLog)) { throw 'Existing render output' }
 $cacheStart = [DateTime]::UtcNow.ToString('o')
 & $cacheRenderer $cacheRender.Flag $cacheOutput *> $cacheLog
 $cacheExit = $LASTEXITCODE
 $cacheResults += [ordered]@{ name=$cacheRender.Label; renderer=$cacheRenderer; renderer_sha256=(Get-FileHash $cacheRenderer -Algorithm SHA256).Hash.ToLower(); flag=$cacheRender.Flag; output=$cacheOutput; log=$cacheLog; started_utc=$cacheStart; finished_utc=[DateTime]::UtcNow.ToString('o'); exit_code=$cacheExit }
 $cacheResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'render-results.json') -Encoding utf8
 Write-Output ($cacheRender.Label + ' exit=' + $cacheExit)
 if ($cacheExit -ne 0) { throw 'Render failed; preserve outputs' }
}
