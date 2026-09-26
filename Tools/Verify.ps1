param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.4')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$project=Join-Path $projectRoot 'VSMConductorTrainer.uproject'
$saved=Join-Path $projectRoot 'Saved\Verification'
New-Item -ItemType Directory -Path $saved -Force | Out-Null
$python=(Get-Command python.exe).Source
$fixture=Start-Process -FilePath $python -ArgumentList @('-u', ('"'+(Join-Path $PSScriptRoot 'http_fixture.py')+'"')) -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $saved 'fixture.log') -RedirectStandardError (Join-Path $saved 'fixture-error.log')
try {
    $ready=$false
    for ($i=0; $i -lt 20; $i++) {
        if ($fixture.HasExited) { throw 'HTTP fixture exited; check fixture-error.log' }
        try { $health=Invoke-RestMethod 'http://127.0.0.1:18765/health' -TimeoutSec 1; $ready=$health.fixture -eq 'vsm-http-tests-v1' } catch {}
        if ($ready) { break }
        Start-Sleep -Milliseconds 200
    }
    if (-not $ready) { throw 'HTTP fixture did not start' }
    $started=[DateTime]::UtcNow
    & (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $project '-unattended' '-NullRHI' '-nosplash' '-NoSound' '-ddc=InstalledNoZenLocalFallback' '-ExecCmds=Automation RunTests VSM' '-TestExit=Automation Test Queue Empty' ('-ReportExportPath='+$saved+'\Automation') '-stdout' '-FullStdOutLogOutput' *> (Join-Path $saved 'automation-console.log')
    $result=$LASTEXITCODE
    Get-Content -LiteralPath (Join-Path $saved 'automation-console.log') | Select-String -Pattern 'Test Completed|Result=|Error:|TEST COMPLETE' | Select-Object -Last 30
    $reportPath=Join-Path $saved 'Automation\index.json'
    if (-not (Test-Path -LiteralPath $reportPath) -or (Get-Item -LiteralPath $reportPath).LastWriteTimeUtc -lt $started) { throw 'No fresh automation report' }
    $report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if($report.failed -gt 0 -or $report.notRun -gt 0 -or $report.tests.Count -ne 7) { throw 'The full set of seven Unreal checks did not pass' }
    exit $result
} finally {
    if (-not $fixture.HasExited) { Stop-Process -Id $fixture.Id -Force }
}
