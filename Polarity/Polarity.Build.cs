// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Polarity : ModuleRules
{
    public Polarity(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "AIModule",
            "StateTreeModule",
            "GameplayStateTreeModule",
            "NavigationSystem",
            "UMG",
            "Slate",
            "PhysicsCore",
            "Niagara",
            "LevelSequence",
            "MovieScene",
            "IKRig"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { "EMF_Plugin", "SlateCore", "RHI", "GameplayTags" });

        PublicIncludePaths.AddRange(new string[] {
            "Polarity",
            "Polarity/AI",
            "Polarity/AI/Components",
            "Polarity/AI/Coordination",
            "Polarity/AI/StateTree",
            "Polarity/Checkpoint",
            "Polarity/Variant_Horror",
            "Polarity/Variant_Horror/UI",
            "Polarity/Variant_Shooter",
            "Polarity/Variant_Shooter/AI",
            "Polarity/Variant_Shooter/UI",
            "Polarity/Variant_Shooter/Weapons"
        });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}