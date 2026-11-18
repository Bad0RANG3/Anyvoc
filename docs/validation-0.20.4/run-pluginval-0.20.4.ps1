$ErrorActionPreference = 'Stop'
$validator = (Resolve-Path 'tools/pluginval/pluginval.exe').Path
$bundle = (Resolve-Path 'build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3').Path
$stdout = Join-Path $PSScriptRoot 'pluginval-0.20.4.txt'
$stderr = Join-Path $PSScriptRoot 'pluginval-0.20.4-stderr.txt'
if ((Test-Path -LiteralPath $stdout) -or (Test-Path -LiteralPath $stderr)) { throw 'Existing validation output; keep originals' }
$started = [DateTime]::UtcNow.ToString('o')
$validatorArguments = '--strictness-level 5 --validate "{0}"' -f $bundle
$validationProcess = Start-Process -FilePath $validator -ArgumentList $validatorArguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$validationText = Get-Content -LiteralPath $stdout -Raw
$seed = [regex]::Match($validationText, 'Random seed: (0x[0-9a-fA-F]+)').Groups[1].Value
$passed = $validationProcess.ExitCode -eq 0 -and $validationText -match 'Anyvoc v0\.20\.4' -and $validationText -match 'Strictness level: 5' -and $validationText.TrimEnd().EndsWith('SUCCESS') -and $seed.Length -gt 0
[ordered]@{ status = $(if ($passed) { 'PASS' } else { 'FAIL' }); started_utc = $started; completed_utc = [DateTime]::UtcNow.ToString('o'); exit_code = $validationProcess.ExitCode; seed = $seed; arguments = $validatorArguments; validator = $validator; bundle = $bundle; stdout = $stdout; stderr = $stderr } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'pluginval-result-0.20.4.json') -Encoding UTF8
Write-Output ('pluginval exit=' + $validationProcess.ExitCode + ' seed=' + $seed + ' passed=' + $passed)
if (!$passed) { throw 'pluginval did not pass strictness 5; logs retained' }
