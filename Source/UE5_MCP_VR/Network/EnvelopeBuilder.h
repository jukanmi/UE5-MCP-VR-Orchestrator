// File: EnvelopeBuilder.h
// Role: UE5 → Python으로 전송할 JSON Envelope를 생성하는 정적 유틸리티.
//
// WHY 이 클래스가 필요한가:
//   WebSocketClient가 직접 JSON 문자열을 조립하면 오타·누락 위험이 크고
//   코드가 비대해진다. 빌더를 분리하면 Envelope 규격 변경 시 이 파일만 수정하면 된다.
//
// 사용법:
//   FString Envelope = FEnvelopeBuilder::BuildPrompt(PayloadJson);
//   WebSocketClient->SendData(Envelope);
//
// Config 설정 (Config/DefaultGame.ini):
//   [OmniAgent]
//   AuthToken=omniagent-dev-secret-changeme-before-production
//   StalePacketThresholdSeconds=2.0

#pragma once

#include "CoreMinimal.h"

// 메시지 타입 Enum (Python EEnvelopeType과 1:1 대응)
enum class EEnvelopeType : uint8
{
    StateUpdate,    // "state_update" - 주기적 월드 상태 동기화
    Prompt,         // "prompt"       - 플레이어 명령/대화
    ActionFailed,   // "action_failed"- UE5에서 명령 실행 실패 통보
    EmergencyReport,// "emergency_report" - 대규모 피격 등 긴급 상황 보고 (N:1)
    LocationDecision,// "location_decision" - EQS 후보 → LLM 전술 위치 결정 요청
};

class UE5_MCP_VR_API FEnvelopeBuilder
{
public:
    // ─────────────────────────────────────────────────────────────────────
    // 메인 Envelope 빌더 함수들
    // 각 함수는 공통 메타데이터(msg_id, auth_token, timestamp)를 자동으로 채운다.
    // ─────────────────────────────────────────────────────────────────────

    /**
     * prompt Envelope 생성. 플레이어 음성/제스처 명령 전송 시 사용.
     * @param PayloadJson - PromptPayload를 직렬화한 JSON 문자열
     * @return            - 완성된 Envelope JSON 문자열
     */
    static FString BuildPrompt(const FString& PayloadJson);

    /**
     * action_failed Envelope 생성. UE5에서 명령 실행이 실패했을 때 호출.
     * @param RefMsgId    - 실패한 명령의 원본 msg_id (Python이 추적에 사용)
     * @param PayloadJson - ActionFailedPayload를 직렬화한 JSON 문자열
     * @return            - 완성된 Envelope JSON 문자열
     */
    static FString BuildActionFailed(const FString& RefMsgId, const FString& PayloadJson);

    /**
     * emergency_report Envelope 생성. NPC의 긴급 이벤트를 묶어서 전송.
     * @param PayloadJson - NPC의 이벤트를 담은 JSON 배열 문자열
     * @return            - 완성된 Envelope JSON 문자열
     */
    static FString BuildEmergencyReport(const FString& PayloadJson);

    /**
     * location_decision Envelope 생성.
     * EQS가 뽑은 후보 위치들을 LLM에 전달하여 최적 위치를 선택하게 한다.
     * WHY: 모든 이동 좌표를 LLM이 직접 생성하면 지연·비용이 크다.
     *      C++에서 EQS+스코어링으로 후보를 추려낸 뒤, 경량 판단만 LLM에 요청한다.
     * @param PayloadJson - FLocationDecisionRequest를 직렬화한 JSON 문자열
     * @return            - 완성된 Envelope JSON 문자열
     */
    static FString BuildLocationDecisionRequest(const FString& PayloadJson);

private:
    // ─────────────────────────────────────────────────────────────────────
    // 내부 헬퍼
    // ─────────────────────────────────────────────────────────────────────

    /** Envelope 타입 Enum → JSON 문자열 변환 (e.g., EEnvelopeType::Prompt → "prompt") */
    static FString EnvelopeTypeToString(EEnvelopeType Type);

    /**
     * 공통 메타데이터를 포함한 Envelope JSON 빌드.
     * WHY: 중복 로직 제거. 모든 BuildXxx 함수는 이 함수를 통해 일관성을 유지.
     *
     * @param Type        - 메시지 타입
     * @param PayloadJson - 타입별 payload JSON 문자열
     * @param RefMsgId    - 참조 msg_id (action_failed 전용, 나머지는 빈 문자열)
     * @return            - 완성된 Envelope JSON 문자열
     */
    static FString BuildEnvelope(
        EEnvelopeType   Type,
        const FString&  PayloadJson,
        const FString&  RefMsgId = TEXT("")
    );

    /** Config/DefaultGame.ini [OmniAgent] AuthToken 값을 1회 로드하여 반환. */
    static FString LoadAuthToken();
};
