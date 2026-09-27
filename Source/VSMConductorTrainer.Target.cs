using UnrealBuildTool;

public class VSMConductorTrainerTarget : TargetRules
{
    public VSMConductorTrainerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("VSMConductorTrainer");
    }
}
