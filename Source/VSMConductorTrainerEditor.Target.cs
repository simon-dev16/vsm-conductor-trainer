using UnrealBuildTool;

public class VSMConductorTrainerEditorTarget : TargetRules
{
    public VSMConductorTrainerEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
        ExtraModuleNames.Add("VSMConductorTrainer");
    }
}
