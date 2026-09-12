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
    STATE_UPDATE = "state_update"  # UE5 상태 주기 동기화
    PROMPT = "prompt"  # 플레이어 명령/대화
    EMERGENCY_REPORT = "emergency_report"  # 긴급 이벤트 배치 전송
    LOCATION_DECISION = "location_decision"  # EQS 후보 → LLM 전술 위치 결정 요청


# ─────────────────────────────────────────────────────────────────────────────
# Payload 모델들 (타입별 내부 데이터 구조)
# WHY: 하나의 거대한 dict 대신 목적별 Pydantic 모델로 분리하여
#      타입 안전성을 보장하고 에러 발생 위치를 즉시 파악 가능하게 한다.
# ─────────────────────────────────────────────────────────────────────────────


class PerceptionData(BaseModel):
    """순수 시각/청각 인지 정보 (FPerceptionData 대응)."""

    target_id: str
    sense_type: str  # "Sight", "Hearing", "Other"
    distance: float
    danger_score: float = 0.0  # C++ FPerceptionData.DangerScore 대응
    in_line_of_sight: bool = False
    location: Dict[str, float]  # {"x", "y", "z"}
    activity_context: str = "Idle"


class EQSQueryResult(BaseModel):
    """EQS 공간 쿼리의 최적 좌표 결과 (상위 1~3개만 전송)."""

    label: str  # e.g. "best_cover", "best_attack_pos"
    x: float
    y: float
    z: float
    score: float = 0.0


class StateUpdatePayload(BaseModel):
    """
    state_update 타입의 payload.
    WHY: UE5의 주기적 상태 스냅샷.
    """

    owner_agent_id: str  # C++ FGameStateData.OwnerAgentID
    current_mode: str = "Common"  # C++ FGameStateData.CurrentMode
    owner_location: Dict[str, float]  # {"x": float, "y": float, "z": float}
    threat_level: str = "None"  # "None", "Low", "Medium", "High"
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

    # ── 계획 캐싱 (Multi-NPC Cached Planning) ──────────────────────
    # WHY: 매 prompt 마다 12B 정제를 도는 낭비를 막는다. C++(UE5)이 perception/
    #      턴 카운터로 재계획 필요 여부를 판정해 전달한다.
    #   requires_replan=True  → 풀 파이프라인(e4b×N → 12B 정제+plan 산출)
    #   requires_replan=False → e4b 단독 경량 루프, current_plan 컨텍스트만 주입
    # 미지정 시 안전하게 풀 파이프라인(True) — 첫 턴/필드 누락 방어.
    requires_replan: bool = True
    # UE5 가 보관 중인 NPC별 plan (replan=False 시 e4b 컨텍스트 주입용). npc_id → plan 구조체.
    current_plan: Optional[Dict[str, Any]] = None
    # 대화 대상 NPC 들의 인벤토리 — UE5 NPCInventoryComponent::GetInventoryJson() 동적 산출.
    # npc_id → [{id,name,desc,count,weight}, ...]. LLM 컨텍스트 주입용(GiveItem/HandObject 근거).
    npc_inventory: Optional[Dict[str, List[Dict[str, Any]]]] = None
    # 유효 액션 타깃 vocabulary — UE5 ResolveActionTarget 해석 가능 키워드 전체
    # (Player/Self/Enemy + 등록 AgentID + 반경 내 빈 가구 ID). Stage1 구조화 스키마 target enum 강제용.
    # 미지정 시 enum 강제 없이 자유문자열 허용(하위호환).
    valid_targets: Optional[List[str]] = None
    # 대상 NPC 반경 내 가구 인지 컨텍스트 — [{id,type,occupied,dist_m}, ...]. UE5 NPCManager 동봉.
    # Sit/Sleep target 지정 근거(natural_context "Nearby furniture:" 조각 소스).
    nearby_furniture: Optional[List[Dict[str, Any]]] = None


class EmergencyReportPayload(BaseModel):
    """
    emergency_report 타입의 payload.
    WHY: NPC가 위험 상황이나 소음을 감지했을 때 즉각적인 대응을 위해 서버로 전송.
    """

    agent_id: str  # 이벤트를 감지한 주체 NPC ID
    perceptions: List[PerceptionData]  # 감지된 이벤트 목록 (위험도순 정렬됨)
    generated_at: float  # 리포트 생성 시각 (UNIX)
    # 보고 성격. "perception"(기본 — C++ 일반 보고는 필드 생략) 외 특수 이벤트 구분용.
    # "combat_victory": 타겟 사망 승리 보고 — danger 게이트 우회, 메모리 기록만(무행동 응답).
    report_type: str = "perception"
    # C++ 척수반사가 이 보고 직전에 실행한 액션 이름("Attack" 등). 미발동이면 C++ 이 필드를
    # 생략하므로 빈 문자열. 채워져 있으면 메모리에 Event 로 남겨 다음 replan 이 중복 지시를
    # 내지 않게 한다.
    reflex_action: str = ""


class LocationCandidate(BaseModel):
    """단일 전술 위치 후보 (C++ FLocationCandidate 대응)."""

    id: str  # e.g. "SAFE_0", "AGGRESSIVE_1"
    category: str  # "SAFE" | "OPTIMAL" | "AGGRESSIVE"
    dist_to_enemy: float
    cover_rating: float  # 0 ~ 1
    height_delta: float  # 양수 = NPC가 더 높음
    score: float


class LocationDecisionPayload(BaseModel):
    """
    location_decision 타입의 payload.
    WHY: C++ EQS가 후보 위치들을 스코어링한 뒤 최종 카테고리 선택을 LLM에 위임.
         LLM은 context_summary와 후보 목록을 보고 chosen_id 하나를 골라 반환한다.
    """

    agent_id: str
    context_summary: str  # "HP:45% Enemies:2 Aggr:60 Fear:30" 등 경량 요약
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
      ref_msg_id : 이 메시지가 참조하는 이전 메시지 ID(예약, 현재 송신 타입 없음).
      auth_token : WebSocket 메시지 수준 인증 토큰. 핸드셰이크 외에도 매 메시지 검증.
      timestamp  : 메시지 생성 시각 (Unix epoch float). Stale 패킷 감지용.
      type       : 메시지 목적 분류 (EEnvelopeType).
      payload    : 실제 데이터. type에 따라 구조가 다름.
    """

    protocol_version: int = 1
    msg_id: str
    ref_msg_id: Optional[str] = None
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

    def parse_emergency_report_payload(self) -> EmergencyReportPayload:
        """payload를 EmergencyReportPayload로 파싱. type이 emergency_report일 때만 호출할 것."""
        return EmergencyReportPayload(**self.payload)

    def parse_location_decision_payload(self) -> LocationDecisionPayload:
        """payload를 LocationDecisionPayload로 파싱. type이 location_decision일 때만 호출할 것."""
        return LocationDecisionPayload(**self.payload)
