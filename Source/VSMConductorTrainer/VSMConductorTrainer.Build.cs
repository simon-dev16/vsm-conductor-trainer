using UnrealBuildTool;

public class VSMConductorTrainer : ModuleRules
{
    public VSMConductorTrainer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "DeveloperSettings", "UMG" });
        PrivateDependencyModuleNames.AddRange(new[] { "HTTP", "Json", "Slate", "SlateCore", "AudioCaptureCore" });
        if (Target.Platform == UnrealTargetPlatform.Android) PrivateDependencyModuleNames.Add("AndroidPermission");
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
    }
}
