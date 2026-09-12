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
            "IKRig",
            "AudioMixer",
            "GeometryCollectionEngine",
            "FieldSystemEngine",
            "ChaosSolverEngine",
            "Foliage",
            "Landscape",
            "LandscapePatch",
            "DeveloperSettings",
            // GameplayTags is PUBLIC, not private: FGameplayTag appears in public headers
            // (UPolarityPalette::Colors, AShooterWeapon::AmmoColorTag).
            "GameplayTags",
            // CableComponent: the grapple line is drawn with UCableComponent, which lives in the
            // engine plugin of the same name. Enabled by default, so no .uproject entry is needed.
            "CableComponent"
        });

        // NetCore: FVector_NetQuantize*::NetSerialize is a header-only wrapper around
        // UE::Net::Write/ReadQuantizedVector, which is NETCORE_API. The Engine module links NetCore
        // for its own move data; a game module that sends its own quantized vector has to as well,
        // or it compiles clean and fails at link with LNK2019 on both symbols.
        // @see FCharacterNetworkMoveData_Polarity::Serialize
        // RecoilAnimation is PRAS, the recoil half of the FPS Animation Pack. Public rather than
        // private because AShooterCharacter holds a URecoilAnimationComponent as a UPROPERTY, so
        // the type has to be visible to anything including that header.
        //
        // NOTE: their module declares "PlatformAllowList": ["Win64"] in FPSAnimationPack.uplugin,
        // so this line pins the whole game module to Win64. That is what we ship today, but it is
        // a real constraint and not an accident: adding a platform means either widening their
        // allow list or wrapping every PRAS reference in a guard.
        PublicDependencyModuleNames.Add("RecoilAnimation");

        PrivateDependencyModuleNames.AddRange(new string[] { "EMF_Plugin", "SlateCore", "RHI", "MoviePlayer", "NetCore" });

        // Editor-only: GC batch creator needs UnrealEd (asset saving) and ContentBrowser (selection)
        if (Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "ContentBrowser", "MeshDescription", "StaticMeshDescription", "GeometryCollectionEditor", "FractureEngine", "Chaos", "DataflowCore" });
        }

        PublicIncludePaths.AddRange(new string[] {
            "Polarity",
            "Polarity/AI",
            "Polarity/AI/Components",
            "Polarity/AI/Coordination",
            "Polarity/AI/Navigation",
            "Polarity/AI/StateTree",
            "Polarity/Checkpoint",
            "Polarity/Music",
            "Polarity/Variant_Shooter",
            "Polarity/Variant_Shooter/AI",
            "Polarity/Variant_Shooter/UI",
            "Polarity/Variant_Shooter/Weapons",
            "Polarity/Subtitle",
            "Polarity/Arena",
            "Polarity/Upgrades",
            "Polarity/Upgrades/Upgrades",
            "Polarity/Variant_Shooter/Pickups",
            "Polarity/Variant_Shooter/Run",
            "Polarity/Variant_Shooter/XP",
            "Polarity/Variant_Shooter/Stream",
            "Polarity/Variant_Shooter/Lore",
            "Polarity/Save",
            "Polarity/Buildings",
            "Polarity/Foliage"
        });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
