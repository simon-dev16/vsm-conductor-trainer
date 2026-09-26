param([switch]$Portrait,[switch]$Packaged,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.4')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$label=if($Portrait){'Portrait'}else{'Landscape'}
if($Packaged){$label='Packaged-'+$label}
$saved=Join-Path $projectRoot ('Saved\Verification\'+$label)
New-Item -ItemType Directory -Path $saved -Force | Out-Null
$arguments=@('-RenderOffscreen','-windowed','-ForceRes','-unattended','-nosplash','-NoSound','-VSMMock','-ExecCmds=Automation RunTests VSM.Visual.Carriage','-TestExit=Automation Test Queue Empty',('-VSMCaptureDir='+$saved),('-abslog='+$saved+'\visual.log'))
if($Portrait){$arguments+=@('-ResX=540','-ResY=960')}else{$arguments+=@('-ResX=1280','-ResY=720')}
if($Packaged){
    $exe=Join-Path $projectRoot 'Build\Artifacts\Windows\VSMConductorTrainer\Binaries\Win64\VSMConductorTrainer.exe'
}else{
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $arguments=@((Join-Path $projectRoot 'VSMConductorTrainer.uproject'),'-game','-ddc=InstalledNoZenLocalFallback')+$arguments
}
$quotedArguments=$arguments | ForEach-Object { '"'+$_.Replace('"','\"')+'"' }
$process=Start-Process -FilePath $exe -ArgumentList ($quotedArguments -join ' ') -WorkingDirectory $projectRoot -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $saved 'console.log') -RedirectStandardError (Join-Path $saved 'stderr.log')
$code=$process.ExitCode
$log=Get-Content -LiteralPath (Join-Path $saved 'visual.log') -Raw
if($log -notmatch 'Test Completed. Result=\{Success\} Name=\{Carriage\}') { throw 'Visual automation did not complete successfully' }
Get-ChildItem -LiteralPath $saved -Filter '*.png' | Select-Object Name,Length
exit $code
