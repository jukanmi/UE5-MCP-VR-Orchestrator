#include "IOpenXRExtensionPlugin.h"
#include "Modules/ModuleManager.h"
#include "OpenXRCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaSimultaneousHands, Log, All);

// UE 5.5 의 openxr.h(1.0.27)에는 XR_META_simultaneous_hands_and_controllers 가 없어 Khronos openxr.h 와 같은 값으로 직접 둔다.
namespace MetaSimultaneous
{
    constexpr const ANSICHAR* ExtensionName = "XR_META_simultaneous_hands_and_controllers";
    constexpr XrStructureType TypeSystemProperties = static_cast<XrStructureType>(1000532001);
    constexpr XrStructureType TypeResumeInfo = static_cast<XrStructureType>(1000532002);

    struct FSystemProperties
    {
        XrStructureType type;
        void* next;
        XrBool32 supportsSimultaneousHandsAndControllers;
    };

    struct FResumeInfo
    {
        XrStructureType type;
        const void* next;
    };

    typedef XrResult(XRAPI_PTR* PFN_Resume)(XrSession session, const FResumeInfo* resumeInfo);
}

/**
 * 한 손은 컨트롤러, 다른 손은 핸드트래킹 — Quest 런타임은 컨트롤러가 켜져 있으면 핸드트래킹을 넘기지 않는다.
 * 확장을 요청하고 세션이 돌기 시작하면 동시 추적을 재개한다. 그 뒤 손마다 핸드트래킹이 잡히는 손은 손으로,
 * 아니면 컨트롤러로 움직인다(AVRPawn::UpdateGhostHandTracking 이 손마다 따로 고른다).
 * 런타임이 확장을 지원하지 않으면 아무것도 하지 않는다(종전 동작).
 */
class FMetaSimultaneousHandsModule : public IModuleInterface, public IOpenXRExtensionPlugin
{
public:
    virtual void StartupModule() override { RegisterOpenXRExtensionModularFeature(); }
    virtual void ShutdownModule() override { UnregisterOpenXRExtensionModularFeature(); }

    virtual FString GetDisplayName() override { return TEXT("MetaSimultaneousHands"); }

    virtual bool GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions) override
    {
        OutExtensions.Add(MetaSimultaneous::ExtensionName);
        return true;
    }

    virtual void PostCreateInstance(XrInstance InInstance) override { Instance = InInstance; }

    virtual void PostGetSystem(XrInstance InInstance, XrSystemId InSystem) override
    {
        // 확장이 켜졌어도 기기가 동시 추적을 못 할 수 있다 — 시작 로그로 갈라 본다.
        MetaSimultaneous::FSystemProperties Simultaneous{ MetaSimultaneous::TypeSystemProperties, nullptr, XR_FALSE };
        XrSystemProperties Props{ XR_TYPE_SYSTEM_PROPERTIES, &Simultaneous };
        if (XR_SUCCEEDED(xrGetSystemProperties(InInstance, InSystem, &Props)))
        {
            UE_LOG(LogMetaSimultaneousHands, Log, TEXT("동시 추적 지원: %s"), Simultaneous.supportsSimultaneousHandsAndControllers ? TEXT("예") : TEXT("아니오"));
        }
    }

    virtual void OnDestroySession(XrSession InSession) override { bResumeTried = false; }

    // 액션 동기화 뒤 = 세션이 실행 중인 게임 스레드. 세션마다 한 번 재개한다.
    virtual void PostSyncActions(XrSession InSession) override
    {
        if (bResumeTried || Instance == XR_NULL_HANDLE) return;
        bResumeTried = true;

        // 확장이 켜지지 않았으면 함수를 못 얻는다(XR_ERROR_FUNCTION_UNSUPPORTED).
        MetaSimultaneous::PFN_Resume Resume = nullptr;
        if (XR_FAILED(xrGetInstanceProcAddr(Instance, "xrResumeSimultaneousHandsAndControllersTrackingMETA", reinterpret_cast<PFN_xrVoidFunction*>(&Resume))) || !Resume)
        {
            UE_LOG(LogMetaSimultaneousHands, Log, TEXT("확장 없음 — 컨트롤러와 핸드트래킹 동시 사용 안 함"));
            return;
        }
        const MetaSimultaneous::FResumeInfo Info{ MetaSimultaneous::TypeResumeInfo, nullptr };
        const XrResult Result = Resume(InSession, &Info);
        UE_LOG(LogMetaSimultaneousHands, Log, TEXT("동시 추적 재개: %s (%d)"), XR_SUCCEEDED(Result) ? TEXT("성공") : TEXT("실패"), static_cast<int32>(Result));
    }

private:
    XrInstance Instance = XR_NULL_HANDLE;
    bool bResumeTried = false;
};

IMPLEMENT_MODULE(FMetaSimultaneousHandsModule, MetaSimultaneousHands)
