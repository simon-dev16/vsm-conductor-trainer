param([switch]$Live,[switch]$Portrait,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.4',[string]$ApiBaseUrl='http://127.0.0.1:18767/v1')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $projectRoot 'Build\Artifacts\Windows\VSMConductorTrainer.exe'
$launchArgs='-windowed -ResX=1280 -ResY=720 -nosplash'
if($Portrait) { $launchArgs='-windowed -ResX=540 -ResY=960 -nosplash' }
if($Live) {
    if($ApiBaseUrl -match '["\s]') { throw 'API URL must not contain spaces or quotes' }
    $launchArgs+=' -VSMLive -VSMAllowLoopback -VSMApiBaseUrl='+$ApiBaseUrl
} else { $launchArgs+=' -VSMMock' }
if(-not (Test-Path -LiteralPath $exe)) {
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $launchArgs='"'+(Join-Path $projectRoot 'VSMConductorTrainer.uproject')+'" -game -ddc=InstalledNoZenLocalFallback '+$launchArgs
}
Start-Process -FilePath $exe -ArgumentList $launchArgs -WorkingDirectory $projectRoot
