// File: OmniAgentConfig.h
// Role: Config/DefaultGame.ini 의 [OmniAgent] 섹션 설정을 읽는 단일 진입점.
//
// WHY:
//   - 서버 주소를 소스코드에 하드코딩하지 않는다. Quest 스탠드얼론 빌드는
//     PC 의 LAN IP 가 필요하므로 빌드별로 .ini 만 바꿔서 대응한다.
//   - LLM/TTS/AuthToken 등 [OmniAgent] 키를 모두 한 곳에서 관리한다.
//
// DefaultGame.ini 예시:
//   [OmniAgent]
//   AuthToken=omniagent-dev-secret-changeme-before-production
//   ServerHost=127.0.0.1   ; Quest 빌드 시 PC LAN IP 로 교체
//   LLMPort=8000
//   TTSPort=8001
//
// NOTE: 값은 최초 1회만 읽어 캐시한다(Config I/O 비용 회피).

#pragma once

#include "CoreMinimal.h"

class UE5_MCP_VR_API FOmniAgentConfig
{
public:
    /** [OmniAgent] AuthToken — Python 서버 .env WS_AUTH_TOKEN 과 일치해야 한다. */
    static FString LoadAuthToken();

    /** [OmniAgent] ServerHost — LLM·TTS 공통 서버 호스트. 미설정 시 127.0.0.1. */
    static FString GetServerHost();

    /** [OmniAgent] LLMPort — LLM WebSocket 포트. 미설정 시 8000. */
    static int32 GetLLMPort();

    /** [OmniAgent] TTSPort — TTS WebSocket 포트. 미설정 시 8001. */
    static int32 GetTTSPort();

    /** ServerHost+LLMPort 로 조립한 LLM WebSocket URL (ws://host:port/ws/llm). */
    static FString GetLLMWebSocketURL();

    /**
     * TTS 서버가 보낸 ws_url 을 완전한 ws:// URL 로 정규화한다.
     *  - 경로만 온 경우(/ws/tts/stream/{id}) → ws://ServerHost:TTSPort 를 앞에 붙임.
     *  - host:port 가 포함된 경우 → 그 부분을 [OmniAgent] ServerHost+TTSPort 로 치환.
     * scheme(ws/wss)·path 는 보존한다.
     *
     * WHY: TTS 서버는 자신의 외부 주소를 알 수 없다. 그 URL 을 받는 Quest 입장에서
     *      127.0.0.1 은 Quest 자신이므로, 호스트 진실은 .ini 한 곳에만 둔다.
     */
    static FString RewriteTTSUrl(const FString& OriginalUrl);
};
