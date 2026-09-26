param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.4')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$saved=Join-Path $root 'Saved\Verification\V2'
New-Item -ItemType Directory -Path $saved -Force | Out-Null
$fixture=Start-Process -FilePath (Get-Command python.exe).Source -ArgumentList @('-u',('"'+(Join-Path $PSScriptRoot 'v2_water_fixture.py')+'"'),'--drop-take-response') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $saved 'fixture.log') -RedirectStandardError (Join-Path $saved 'fixture-error.log')
try {
    $ready=$false
    for($i=0;$i -lt 30;$i++){
        if($fixture.HasExited){throw 'Fixture exited unexpectedly'}
        if((Get-Content (Join-Path $saved 'fixture.log') -Raw -ErrorAction SilentlyContinue) -match 'FIXTURE'){$ready=$true;break}
        Start-Sleep -Milliseconds 200
    }
    if(!$ready){throw 'Fixture did not start'}
    $started=[DateTime]::UtcNow
    & (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') (Join-Path $root 'VSMConductorTrainer.uproject') '-unattended' '-NullRHI' '-nosplash' '-NoSound' '-VSMApiBaseUrl=http://127.0.0.1:18768' '-ExecCmds=Automation RunTests VSM.V2' '-TestExit=Automation Test Queue Empty' ('-ReportExportPath='+$saved+'\Automation') '-stdout' '-FullStdOutLogOutput' *> (Join-Path $saved 'unreal.log')
    $code=$LASTEXITCODE
    Get-Content (Join-Path $saved 'unreal.log') | Select-String -Pattern 'Test Completed|Result=|Error:|TEST COMPLETE' | Select-Object -Last 20
    $path=Join-Path $saved 'Automation\index.json'
    if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw 'Fresh automation report missing'}
    $report=Get-Content $path -Raw | ConvertFrom-Json
    if($report.failed -ne 0 -or ($report.succeeded+$report.succeededWithWarnings) -lt 1 -or $report.notRun -ne 0){throw 'V2 lifecycle failed'}
    exit $code
} finally {
    if(!$fixture.HasExited){Stop-Process -Id $fixture.Id}
}

