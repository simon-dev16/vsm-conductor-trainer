using UnrealBuildTool;

public class VSMConductorTrainer : ModuleRules
{
    public VSMConductorTrainer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "DeveloperSettings", "UMG" });
        PrivateDependencyModuleNames.AddRange(new[] { "HTTP", "Json", "Slate", "SlateCore", "AudioCaptureCore", "LevelSequence", "MovieScene", "MovieSceneTracks", "AnimGraphRuntime" });
        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            PrivateDependencyModuleNames.Add("AndroidPermission");
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", System.IO.Path.Combine(ModuleDirectory, "Android", "LocalNetwork_UPL.xml"));
        }
        if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "UMGEditor", "AnimGraph", "BlueprintGraph", "KismetCompiler" });

    }
}
