param([string]$Exe = "$PSScriptRoot\..\dist\CppToolbox.exe", [switch]$Preview)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Exe)) { throw "Toolbox executable is missing: $Exe" }
$arguments = '--self-test'
if ($Preview) { $arguments += ' --preview' }
$p = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1200
$second = Start-Process -FilePath $Exe -ArgumentList '--self-test' -PassThru -WindowStyle Hidden
if (!$second.WaitForExit(5000)) { throw 'Second instance did not exit' }
if ($second.ExitCode -ne 0) { throw 'Single-instance activation failed' }
if (!$p.WaitForExit(45000)) { throw 'Integration test timed out' }
if ($p.ExitCode -ne 0) { throw "Integration test failed: $($p.ExitCode)" }
Write-Output 'Toolbox integration checks passed.'
