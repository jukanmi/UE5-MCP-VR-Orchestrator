"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: state.py                                                              ║
║ Role: SHARED STATE DEFINITION (Data Contract)                              ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Define the shared state object that flows through the entire LangGraph    ║
║   pipeline. Acts as the "blackboard" for inter-agent communication.         ║
║                                                                              ║
║ STATE LIFECYCLE (Section 8 Orchestra):                                      ║
║   1. UE5 sends vr_context (GesPrompt) via Envelope                         ║
║   2. Interface Input adds natural_context                                   ║
║   3. Dialogue adds raw_response                                             ║
║   4. Interface Output adds action_batch                                     ║
║   5. Rules validates action_batch                                           ║
║   6. UE5 receives final action_batch                                        ║
║                                                                              ║
║ FIELD CATEGORIES:                                                            ║
║   • Input:     vr_context (UE5에서 수신)                                   ║
║   • Cache:     cached_world_state (state_update 수신 시만 갱신, LLM 미호출) ║
║   • History:   failed_action_history (action_failed 이력 누적)              ║
║   • Pipeline:  natural_context, raw_response, target_npc                    ║
║   • Routing:   next, current_speaker                                        ║
║   • Output:    action_batch (UE5로 전송)                                   ║
║   • Safety:   has_error, error_msg (보안 차단), target_npcs (라우팅 가드) ║
║                                                                              ║
║ IMMUTABILITY:                                                                ║
║   Field names and types are stable. New agents may ADD fields but must      ║
║   never REMOVE or RENAME existing fields without migration plan.            ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
from typing import TypedDict, Annotated, List, Optional, Dict, Any
from langgraph.graph.message import add_messages
from ..schemas.vr_context import GesPrompt
from ..schemas.actions import ActionBatch


class AgentState(TypedDict):
    """
    LangGraph 파이프라인 전체에서 흐르는 공유 상태 객체.
    새 필드 추가 시 반드시 Optional 또는 기본값을 지정하여 하위 호환성을 유지할 것.
    """

    # ── 입력 (UE5 → Python) ──────────────────────────────────────────
    # 플레이어의 음성/제스처 명령. prompt 타입 Envelope의 payload에서 파싱됨.
    vr_context: Optional[GesPrompt]

    # [신규] state_update 수신 시 캐시되는 최신 월드 상태
    # WHY: LLM 파이프라인 없이 상태만 저장하여, 다음 prompt 처리 시
    #      "현재 환경 컨텍스트"로 활용한다. 매 요청마다 덮어씌운다.
    cached_world_state: Optional[Dict[str, Any]]

    # [신규] action_failed 실패 이력 누적 목록
    # WHY: Python이 내린 명령이 UE5에서 실패할 경우, 그 이유를
    #      다음 추론 컨텍스트에 포함시켜 동일 실수를 반복하지 않게 한다.
    #      리스트에 append하는 방식으로 누적. 최대 N개 유지는 Interface Input 에이전트가 담당.
    failed_action_history: List[Dict[str, Any]]

    # ── 파이프라인 중간 상태 ─────────────────────────────────────────
    # Interface Input → Dialogue: 자연어로 변환된 플레이어 컨텍스트
    natural_context: Optional[str]

    # Dialogue → Interface Output: LLM이 생성한 NPC 원본 응답
    raw_response: Optional[str]

    # 대상 NPC ID (Supervisor가 결정)
    target_npc: Optional[str]

    # Dialogue 에이전트가 결정하는 행동 모드 / 표정
    behavior_mode: Optional[str]
    facial_state: Optional[str]

    # ── 내부 라우팅 상태 ─────────────────────────────────────────────
    next: str
    current_speaker: str

    # ── 최종 출력 (Python → UE5) ─────────────────────────────────────
    # Rules 검증 후 UE5로 전송할 ActionBatch
    action_batch: Optional[ActionBatch]

    # ── LangGraph 메시지 히스토리 ────────────────────────────────────
    messages: Annotated[List[Any], add_messages]

    # ── 보안 및 라우팅 가드레일 ────────────────────────────────────
    target_npcs: List[str]
    msg_id: str
    timestamp: float
    has_error: bool
    error_msg: Optional[str]
