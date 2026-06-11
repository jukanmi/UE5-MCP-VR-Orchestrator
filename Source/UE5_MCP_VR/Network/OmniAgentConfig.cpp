// File: OmniAgentConfig.cpp
// Role: FOmniAgentConfig 구현부 — [OmniAgent] 섹션 1회 로드 + 캐시.

#include "OmniAgentConfig.h"
#include "Misc/ConfigCacheIni.h"

// ─────────────────────────────────────────────────────────────────────────────
// 캐시: Config 파일 I/O 는 비용이 있으므로 최초 1회만 읽는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    bool    bAuthTokenLoaded = false;
    FString CachedAuthToken;

    bool    bServerHostLoaded = false;
    FString CachedServerHost;

    bool    bLLMPortLoaded = false;
    int32   CachedLLMPort = 8000;

    bool    bTTSPortLoaded = false;
    int32   CachedTTSPort = 8001;

    const TCHAR* OmniAgentSection = TEXT("OmniAgent");
}

FString FOmniAgentConfig::LoadAuthToken()
{
    if (bAuthTokenLoaded)
    {
        return CachedAuthToken;
    }

    GConfig->GetString(OmniAgentSection, TEXT("AuthToken"), CachedAuthToken, GGameIni);

    if (CachedAuthToken.IsEmpty())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[OmniAgentConfig] Config/DefaultGame.ini 에 [OmniAgent] AuthToken 이 없습니다. "
                 "Python 서버 .env 의 WS_AUTH_TOKEN 과 일치하는 값을 추가하세요."));
        CachedAuthToken = TEXT(""); // 빈 문자열 → Python 에서 인증 거부됨
    }

    bAuthTokenLoaded = true;
    return CachedAuthToken;
}

FString FOmniAgentConfig::GetServerHost()
{
    if (bServerHostLoaded)
    {
        return CachedServerHost;
    }

    GConfig->GetString(OmniAgentSection, TEXT("ServerHost"), CachedServerHost, GGameIni);

    if (CachedServerHost.IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OmniAgentConfig] [OmniAgent] ServerHost 미설정 — 127.0.0.1 로 폴백. "
                 "Quest 빌드는 PC 의 LAN IP 로 설정해야 합니다."));
        CachedServerHost = TEXT("127.0.0.1");
    }

    bServerHostLoaded = true;
    return CachedServerHost;
}

int32 FOmniAgentConfig::GetLLMPort()
{
    if (!bLLMPortLoaded)
    {
        if (!GConfig->GetInt(OmniAgentSection, TEXT("LLMPort"), CachedLLMPort, GGameIni))
        {
            CachedLLMPort = 8000;
        }
        bLLMPortLoaded = true;
    }
    return CachedLLMPort;
}

int32 FOmniAgentConfig::GetTTSPort()
{
    if (!bTTSPortLoaded)
    {
        if (!GConfig->GetInt(OmniAgentSection, TEXT("TTSPort"), CachedTTSPort, GGameIni))
        {
            CachedTTSPort = 8001;
        }
        bTTSPortLoaded = true;
    }
    return CachedTTSPort;
}

FString FOmniAgentConfig::GetLLMWebSocketURL()
{
    return FString::Printf(TEXT("ws://%s:%d/ws/llm"), *GetServerHost(), GetLLMPort());
}

FString FOmniAgentConfig::RewriteTTSUrl(const FString& OriginalUrl)
{
    if (OriginalUrl.IsEmpty())
    {
        return OriginalUrl;
    }

    // scheme(ws:// / wss://) 분리 — 없으면 ws:// 가정.
    FString Scheme = TEXT("ws://");
    FString AfterScheme = OriginalUrl;
    const int32 SchemeSep = OriginalUrl.Find(TEXT("://"));
    if (SchemeSep != INDEX_NONE)
    {
        Scheme = OriginalUrl.Left(SchemeSep + 3);
        AfterScheme = OriginalUrl.RightChop(SchemeSep + 3);
    }

    // authority(host:port) 와 path 분리. path 가 없으면 빈 문자열.
    FString Path;
    int32 PathSep;
    if (AfterScheme.FindChar(TEXT('/'), PathSep))
    {
        Path = AfterScheme.RightChop(PathSep);
    }

    const FString Rewritten = FString::Printf(
        TEXT("%s%s:%d%s"), *Scheme, *GetServerHost(), GetTTSPort(), *Path);

    if (Rewritten != OriginalUrl)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[OmniAgentConfig] TTS URL 재작성: %s → %s"), *OriginalUrl, *Rewritten);
    }
    return Rewritten;
}
