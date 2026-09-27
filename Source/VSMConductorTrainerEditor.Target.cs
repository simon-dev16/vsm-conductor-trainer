using UnrealBuildTool;

public class VSMConductorTrainerEditorTarget : TargetRules
{
    public VSMConductorTrainerEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("VSMConductorTrainer");
    }
}
