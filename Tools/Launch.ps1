param([switch]$Live,[switch]$Portrait,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$ApiBaseUrl='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $projectRoot 'Build\Artifacts\Windows\VSMConductorTrainer.exe'
$launchArgs='-windowed -ResX=1280 -ResY=720 -nosplash'
if($Portrait) { $launchArgs='-windowed -ResX=540 -ResY=960 -nosplash' }
if($Live) {
    # -VSMApiBaseUrl читают оба клиента сразу, а ждут они разного: клиент v2 сам
    # дописывает /v2/... к адресу, а клиент v1 рассчитывает на адрес, уже оканчивающийся
    # на /v1. Поэтому без явного -ApiBaseUrl ничего не подставляем — адрес берётся из
    # Config/DefaultGame.ini. Раньше здесь стоял адрес с /v1, и v2 получал /v1/v2/... и 404.
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
