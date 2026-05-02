"""

 File: envelope.py                                                           
 Role: JSON 통신 Envelope 스키마 정의 (UE5 ↔ Python 공통 규약)               

 WHY (설계 의도):                                                             
   UE5 → Python 간 모든 메시지는 '봉투(Envelope)' 형태로 감싸서 보낸다.     
   이 봉투에는 인증, 순서 보장, 보안을 위한 메타데이터가 담겨 있으며,       
   내부의 실제 데이터(payload)는 메시지 타입에 따라 구조가 달라진다.        
                                                                              
 MESSAGE TYPE 분류:                                                           
   • state_update : 주기적 NPC 상태 동기화 (LLM 호출 없이 캐싱만)           
   • prompt       : 플레이어 음성/제스처 명령 (LLM 파이프라인 실행)          
   • action_failed: Python이 내린 명령이 UE5에서 실패했음을 알리는 콜백     

"""
from enum import Enum
from pydantic import BaseModel, Field, field_validator
from typing import Optional, Any, Dict, List
import time


# ─────────────────────────────────────────────────────────────────────────────
# 메시지 타입 Enum
# WHY: 문자열 비교 대신 Enum을 사용해 오타로 인한 분기 오류를 원천 차단한다.
# ─────────────────────────────────────────────────────────────────────────────
class EEnvelopeType(str, Enum):
    STATE_UPDATE  = "state_update"   # UE5 상태 주기 동기화
    PROMPT        = "prompt"         # 플레이어 명령/대화
    ACTION_FAILED = "action_failed"  # UE5에서 명령 실행 실패 통보
    EMERGENCY_REPORT = "emergency_report" # 긴급 이벤트 배치 전송
    LOCATION_DECISION = "location_decision" # EQS 후보 → LLM 전술 위치 결정 요청


# ─────────────────────────────────────────────────────────────────────────────
# Payload 모델들 (타입별 내부 데이터 구조)
# WHY: 하나의 거대한 dict 대신 목적별 Pydantic 모델로 분리하여
#      타입 안전성을 보장하고 에러 발생 위치를 즉시 파악 가능하게 한다.
# ─────────────────────────────────────────────────────────────────────────────

class PerceptionData(BaseModel):
    """순수 시각/청각 인지 정보 (FPerceptionData 대응)."""
    target_id: str
    sense_type: str      # "Sight", "Hearing", "Other"
    distance: float
    danger_score: float = 0.0 # C++ FPerceptionData.DangerScore 대응
    in_line_of_sight: bool = False
    location: Dict[str, float]  # {"x", "y", "z"}
    activity_context: str = "Idle"


class EQSQueryResult(BaseModel):
    """EQS 공간 쿼리의 최적 좌표 결과 (상위 1~3개만 전송)."""
    label: str              # e.g. "best_cover", "best_attack_pos"
    x: float
    y: float
    z: float
    score: float = 0.0


class StateUpdatePayload(BaseModel):
    """
    state_update 타입의 payload.
    WHY: UE5의 주기적 상태 스냅샷.
    """
    owner_agent_id: str                         # C++ FGameStateData.OwnerAgentID
    current_mode: str = "Common"                # C++ FGameStateData.CurrentMode
    owner_location: Dict[str, float]            # {"x": float, "y": float, "z": float}
    threat_level: str = "None"                  # "None", "Low", "Medium", "High"
    in_cover: bool = False
    line_of_sight: bool = False
    perceived_targets: List[PerceptionData] = Field(default_factory=list)

    @field_validator("owner_location")
    @classmethod
    def round_coordinates(cls, v: Dict[str, float]) -> Dict[str, float]:
        """LLM 토큰 낭비를 방지하기 위해 좌표 소수점을 2자리로 반올림."""
        return {key: round(val, 2) for key, val in v.items()}


class PromptPayload(BaseModel):
    """
    prompt 타입의 payload.
    WHY: 플레이어가 VR에서 말하거나 제스처를 취할 때 전송되는 구조체.
         기존 GesPrompt를 Envelope 안의 payload로 감싸는 형태.
    """
    player_id: str
    voice_transcript: str
    target_npc_id: Optional[str] = None
    gestures: List[Dict[str, Any]] = Field(default_factory=list)
    player_location: Optional[Dict[str, float]] = None
    last_event: Optional[str] = None
    stats: Optional[Dict[str, float]] = None


class ActionFailedPayload(BaseModel):
    """
    action_failed 타입의 payload.
    WHY: Python이 내린 명령이 UE5 물리/충돌/상태 검증에서 실패했을 때 돌아오는 콜백.
         ref_msg_id를 통해 '어떤 명령'이 실패했는지 추적하여
         다음 추론 시 동일한 실수를 반복하지 않도록 이력에 기록한다.
    """
    failed_action_type: str         # 실패한 액션 종류 (e.g., "Move", "Attack")
    reason: str                     # 실패 이유 (e.g., "PathNotFound", "TargetDead")
    executor_npc_id: str            # 명령을 시도했던 NPC ID


class EmergencyReportPayload(BaseModel):
    """
    emergency_report 타입의 payload.
    WHY: NPC가 위험 상황이나 소음을 감지했을 때 즉각적인 대응을 위해 서버로 전송.
    """
    agent_id: str                               # 이벤트를 감지한 주체 NPC ID
    perceptions: List[PerceptionData]           # 감지된 이벤트 목록 (위험도순 정렬됨)
    generated_at: float                         # 리포트 생성 시각 (UNIX)


class LocationCandidate(BaseModel):
    """단일 전술 위치 후보 (C++ FLocationCandidate 대응)."""
    id: str                  # e.g. "SAFE_0", "AGGRESSIVE_1"
    category: str            # "SAFE" | "OPTIMAL" | "AGGRESSIVE"
    dist_to_enemy: float
    cover_rating: float      # 0 ~ 1
    height_delta: float      # 양수 = NPC가 더 높음
    score: float


class LocationDecisionPayload(BaseModel):
    """
    location_decision 타입의 payload.
    WHY: C++ EQS가 후보 위치들을 스코어링한 뒤 최종 카테고리 선택을 LLM에 위임.
         LLM은 context_summary와 후보 목록을 보고 chosen_id 하나를 골라 반환한다.
    """
    agent_id: str
    context_summary: str             # "HP:45% Enemies:2 Aggr:60 Fear:30" 등 경량 요약
    candidates: List[LocationCandidate]



# ─────────────────────────────────────────────────────────────────────────────
# 최상위 Envelope 모델
# WHY: 모든 UE5 → Python 메시지의 '봉투' 역할.
#      payload는 type에 따라 다른 구조를 가지므로 Any로 선언 후
#      middleware에서 타입별로 캐스팅(parse_payload())하여 처리한다.
# ─────────────────────────────────────────────────────────────────────────────
class MessageEnvelope(BaseModel):
    """
    UE5 ↔ Python 간 모든 메시지의 공통 봉투(Envelope) 구조.

    필드 역할:
      msg_id     : 이 메시지의 고유 ID. 요청-응답 추적 및 중복 감지용.
      ref_msg_id : 이 메시지가 참조하는 이전 메시지 ID. action_failed에서 필수.
      auth_token : WebSocket 메시지 수준 인증 토큰. 핸드셰이크 외에도 매 메시지 검증.
      timestamp  : 메시지 생성 시각 (Unix epoch float). Stale 패킷 감지용.
      type       : 메시지 목적 분류 (EEnvelopeType).
      payload    : 실제 데이터. type에 따라 구조가 다름.
    """
    protocol_version: int = 1
    msg_id: str
    ref_msg_id: Optional[str] = None  # action_failed가 아니면 None 가능
    auth_token: str
    timestamp: float = Field(default_factory=time.time)
    type: EEnvelopeType
    payload: Any  # 타입별 파싱은 middleware.py에서 처리

    def parse_prompt_payload(self) -> PromptPayload:
        """payload를 PromptPayload로 파싱. type이 prompt일 때만 호출할 것."""
        return PromptPayload(**self.payload)

    def parse_state_update_payload(self) -> StateUpdatePayload:
        """payload를 StateUpdatePayload로 파싱. type이 state_update일 때만 호출할 것."""
        return StateUpdatePayload(**self.payload)

    def parse_action_failed_payload(self) -> ActionFailedPayload:
        """payload를 ActionFailedPayload로 파싱. type이 action_failed일 때만 호출할 것."""
        return ActionFailedPayload(**self.payload)

    def parse_emergency_report_payload(self) -> EmergencyReportPayload:
        """payload를 EmergencyReportPayload로 파싱. type이 emergency_report일 때만 호출할 것."""
        return EmergencyReportPayload(**self.payload)
