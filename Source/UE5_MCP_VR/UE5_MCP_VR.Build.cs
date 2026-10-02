// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE5_MCP_VR : ModuleRules
{
	public UE5_MCP_VR(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "AIModule", "GameplayTasks", "NavigationSystem", "HTTP", "GameplayTags", "StateTreeModule", "GameplayStateTreeModule", "HeadMountedDisplay", "XRBase", "DeveloperSettings" });

		PrivateDependencyModuleNames.AddRange(new string[] { "WebSockets", "Json", "JsonUtilities", "AnimationCore" });

		// Uncomment if you are using Slate UI
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// 에디터 전용: NPC 뼈 캡슐 반지름 자동 채움(UNPCBoneCapsuleSet::FillFromMeshes)이 메시 정점을 읽는다.
		// 아이템 충돌 자동 생성(UItemCollisionGen)은 V-HACD 를 직접 부르고 UnrealEd 의 충돌 갱신 함수를 쓴다.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "MeshUtilitiesEngine", "MeshUtilitiesCommon", "UnrealEd", "Chaos" });
			AddEngineThirdPartyPrivateStaticDependencies(Target, "VHACD");
		}

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
