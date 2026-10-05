using UnrealBuildTool;

public class MetaSimultaneousHands : ModuleRules
{
	public MetaSimultaneousHands(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// IOpenXRExtensionPlugin.h 가 AR 핀·엔진 타입 헤더를 함께 끌어온다.
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AugmentedReality", "HeadMountedDisplay", "XRBase", "OpenXRHMD" });

		// openxr.h — OpenXR 확장 플러그인 인터페이스가 XrInstance·XrSession 타입을 쓴다.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");
	}
}
