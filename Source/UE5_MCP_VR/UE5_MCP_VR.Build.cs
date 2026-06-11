// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE5_MCP_VR : ModuleRules
{
	public UE5_MCP_VR(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "AIModule", "GameplayTasks", "NavigationSystem", "HTTP", "GameplayTags", "StateTreeModule", "GameplayStateTreeModule", "HeadMountedDisplay", "XRBase" });

		PrivateDependencyModuleNames.AddRange(new string[] { "WebSockets", "Json", "JsonUtilities" });

		// ASR 음성 입력 — 마이크 캡처 (Audio::FAudioCapture)
		PrivateDependencyModuleNames.AddRange(new string[] { "AudioCapture", "AudioCaptureCore" });

		// Uncomment if you are using Slate UI
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
