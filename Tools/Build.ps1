param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8', [switch]$Package)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$project=Join-Path $projectRoot 'VSMConductorTrainer.uproject'
if ($Package) {
    $log=Join-Path $projectRoot 'Saved\Verification\package.log'
    New-Item -ItemType Directory -Path (Split-Path $log) -Force | Out-Null
    $staleZenProjectStore=Join-Path $projectRoot 'Saved\Cooked\Windows\ue.projectstore'
    if (Test-Path -LiteralPath $staleZenProjectStore) { Remove-Item -LiteralPath $staleZenProjectStore -Force }
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun ('-project='+$project) -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive ('-archivedirectory='+$projectRoot+'\Build\Artifacts') -unattended -utf8output -ddc=InstalledNoZenLocalFallback '-UbtArgs=-MaxParallelActions=3' '-ini:Game:[/Script/UnrealEd.ProjectPackagingSettings]:bUseZenStore=False' *> $log
    Get-Content -LiteralPath $log -Tail 30
} else {
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') VSMConductorTrainerEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=3
}
exit $LASTEXITCODE
