param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.4', [switch]$Package)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$project=Join-Path $projectRoot 'VSMConductorTrainer.uproject'
if ($Package) {
    $log=Join-Path $projectRoot 'Saved\Verification\package.log'
    New-Item -ItemType Directory -Path (Split-Path $log) -Force | Out-Null
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun ('-project='+$project) -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive ('-archivedirectory='+$projectRoot+'\Build\Artifacts') -unattended -utf8output -ddc=InstalledNoZenLocalFallback '-UbtArgs=-MaxParallelActions=3' *> $log
    Get-Content -LiteralPath $log -Tail 30
} else {
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') VSMConductorTrainerEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=3
}
exit $LASTEXITCODE
