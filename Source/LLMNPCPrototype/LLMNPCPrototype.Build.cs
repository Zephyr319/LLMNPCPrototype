// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LLMNPCPrototype : ModuleRules
{
	public LLMNPCPrototype(ReadOnlyTargetRules Target) : base(Target)
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
			"UMG",
			"Slate",
			"HTTP",
            "Json",
            "JsonUtilities",
            "AudioCapture",
            "AudioMixer"

        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"LLMNPCPrototype",
			"LLMNPCPrototype/Variant_Platforming",
			"LLMNPCPrototype/Variant_Platforming/Animation",
			"LLMNPCPrototype/Variant_Combat",
			"LLMNPCPrototype/Variant_Combat/AI",
			"LLMNPCPrototype/Variant_Combat/Animation",
			"LLMNPCPrototype/Variant_Combat/Gameplay",
			"LLMNPCPrototype/Variant_Combat/Interfaces",
			"LLMNPCPrototype/Variant_Combat/UI",
			"LLMNPCPrototype/Variant_SideScrolling",
			"LLMNPCPrototype/Variant_SideScrolling/AI",
			"LLMNPCPrototype/Variant_SideScrolling/Gameplay",
			"LLMNPCPrototype/Variant_SideScrolling/Interfaces",
			"LLMNPCPrototype/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
