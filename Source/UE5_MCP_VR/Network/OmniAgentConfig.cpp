// File: OmniAgentConfig.cpp
// Role: FOmniAgentConfig 구현부 — [OmniAgent] 섹션 1회 로드 + 캐시.

#include "OmniAgentConfig.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
    // Config 파일 I/O 는 비용이 있으므로 세 값을 최초 접근 시 한 번에 읽어 둔다.
    struct FLoaded
    {
        FString AuthToken;
        FString ServerHost;
        int32   LLMPort = 8000;
    };

    const FLoaded& Get()
    {
        static const FLoaded Loaded = []()
        {
            const TCHAR* Section = TEXT("OmniAgent");
            FLoaded L;

            GConfig->GetString(Section, TEXT("AuthToken"), L.AuthToken, GGameIni);
            if (L.AuthToken.IsEmpty())
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[OmniAgentConfig] Config/DefaultGame.ini 에 [OmniAgent] AuthToken 이 없습니다. "
                         "Python 서버 .env 의 WS_AUTH_TOKEN 과 일치하는 값을 추가하세요."));
                // 빈 문자열 → Python 에서 인증 거부됨
            }

            GConfig->GetString(Section, TEXT("ServerHost"), L.ServerHost, GGameIni);
            if (L.ServerHost.IsEmpty())
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[OmniAgentConfig] [OmniAgent] ServerHost 미설정 — 127.0.0.1 로 폴백. "
                         "Quest 빌드는 PC 의 LAN IP 로 설정해야 합니다."));
                L.ServerHost = TEXT("127.0.0.1");
            }

            if (!GConfig->GetInt(Section, TEXT("LLMPort"), L.LLMPort, GGameIni))
            {
                L.LLMPort = 8000;
            }
            return L;
        }();
        return Loaded;
    }
}

FString FOmniAgentConfig::LoadAuthToken() { return Get().AuthToken; }
FString FOmniAgentConfig::GetServerHost() { return Get().ServerHost; }
int32   FOmniAgentConfig::GetLLMPort()    { return Get().LLMPort; }

FString FOmniAgentConfig::GetLLMWebSocketURL()
{
    return FString::Printf(TEXT("ws://%s:%d/ws/llm"), *GetServerHost(), GetLLMPort());
}
