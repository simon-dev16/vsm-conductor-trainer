param([switch]$Live,[switch]$Portrait,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$ApiBaseUrl='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $projectRoot 'Build\Artifacts\Windows\VSMConductorTrainer.exe'
$launchArgs='-windowed -ResX=1280 -ResY=720 -nosplash'
if($Portrait) { $launchArgs='-windowed -ResX=540 -ResY=960 -nosplash' }
if($Live) {
    # The V2 client appends its own /v2 path to the configured base URL.
    if($ApiBaseUrl) {
        if($ApiBaseUrl -match '["\s]') { throw 'API URL must not contain spaces or quotes' }
        $launchArgs+=' -VSMApiBaseUrl='+$ApiBaseUrl
    }
    $launchArgs+=' -VSMLive -VSMAllowLoopback'
} else { $launchArgs+=' -VSMMock' }
if(-not (Test-Path -LiteralPath $exe)) {
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $launchArgs='"'+(Join-Path $projectRoot 'VSMConductorTrainer.uproject')+'" -game -ddc=InstalledNoZenLocalFallback '+$launchArgs
}
Start-Process -FilePath $exe -ArgumentList $launchArgs -WorkingDirectory $projectRoot
