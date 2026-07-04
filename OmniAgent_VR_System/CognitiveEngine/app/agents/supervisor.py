"""

File: supervisor.py
Role: ORCHESTRATOR (파이프라인 오케스트레이터)

핵심 역할 (변경 금지):
  에이전트 간 라우팅 결정. 에러·폴백 처리로 파이프라인 완료를 보장한다.

라우팅 흐름:
  [Error/Empty] → 즉시 End (숏컷, LLM 호출 없음)
  Interface_Input → Dialogue
  Dialogue → Interface_Output
  Interface_Output → Rules
  Rules → End (정상) / Dialogue (거부 1회 재시도) / 폴백 End (재시도 소진)

에러 숏컷 원칙 (청사진 요구사항):
  - has_error=True → 어떤 노드에서든 즉시 End로 우회
  - target_npcs가 비어있어서 처리 대상 없음 → End로 우회
  → 불필요한 LLM 호출을 제거해 토큰·지연 비용 절감

"""

from typing import Literal
from .state import AgentState
from ..schemas.actions import ActionBatch, GameAction


def _route_after_input(state: AgentState) -> dict:
    """1단계: Interface_Input 이후 → Dialogue. natural_context/대상 NPC 없으면 End 숏컷."""
    natural_context = state.get("natural_context", "")
    if not natural_context:
        print("[Supervisor] ⚠️  natural_context 비어있음, 계속 진행")

    # target_npcs 비어있으면 처리 대상 없음 → 즉시 종료
    target_npcs = state.get("target_npcs", [])
    if len(target_npcs) == 0 and not state.get("target_npc"):
        print("[Supervisor] 대상 NPC 없음, 숏컷 → End")
        return {"next": "End"}

    return {"next": "Dialogue", "current_speaker": "Supervisor"}


def _route_after_dialogue(state: AgentState) -> dict:
    """2단계: Dialogue 이후 → Interface_Output. raw_response 비면 폴백 대사 주입."""
    raw_response = state.get("raw_response", "")
    if not raw_response:
        print("[Supervisor] ⚠️  raw_response 비어있음, 폴백 주입")
        return {
            "raw_response": '"..." (confused)',
            "next": "Interface_Output",
            "current_speaker": "Supervisor",
        }
    return {"next": "Interface_Output", "current_speaker": "Supervisor"}


def _route_after_output(state: AgentState) -> dict:
    """3단계: Interface_Output 이후 → Rules. ActionBatch 비면 폴백 배치 생성."""
    action_batch = state.get("action_batch")
    if not action_batch or not action_batch.Actions:
        print("[Supervisor] ⚠️  ActionBatch 비어있음, 폴백 배치 생성")
        npc_id = state.get("target_npc", "Elara")
        return {
            "action_batch": _create_fallback_batch(npc_id),
            "next": "Rules",
            "current_speaker": "Supervisor",
        }
    return {"next": "Rules", "current_speaker": "Supervisor"}


def _route_after_rules(state: AgentState) -> dict:
    """4단계: Rules 이후 → End(정상) / Dialogue(거부 1회 재시도) / 폴백 End(재시도 소진·부분 실패)."""
    # 멀티 NPC: action_batches 우선, 폴백으로 action_batch 단일
    action_batches = state.get("action_batches") or {}
    action_batch = state.get("action_batch")

    # 거부 판정: action_batches 있으면 모든 배치가 비어야 거부, 없으면 단일 배치 기준
    if action_batches:
        empty_ids = [k for k, b in action_batches.items() if not b.Actions]
        is_rejected = len(empty_ids) == len(action_batches)

        # 부분 실패 — 일부 NPC 만 배치 전멸: 해당 NPC 에만 폴백 배치 주입 후 정상 종료.
        # 전체 재시도(Dialogue 왕복)는 정상 NPC 응답까지 지연시키므로 전체 전멸 시에만.
        if not is_rejected and empty_ids:
            print(f"[Supervisor] ⚠️  부분 실패 — 폴백 배치 주입: {empty_ids}")
            for npc in empty_ids:
                action_batches[npc] = _create_fallback_batch(npc)
            first = next(iter(action_batches.values()), None)
            return {
                "action_batches": action_batches,
                "action_batch": first,
                "next": "End",
            }
    else:
        is_rejected = not action_batch or not action_batch.Actions

    if is_rejected:
        retry_count = state.get("rules_retry_count", 0)
        if retry_count >= 1:
            # 재시도 소진 — 각 NPC에 폴백 배치 생성
            npcs = state.get("target_npcs") or [state.get("target_npc", "Elara")]
            print(f"[Supervisor] ❌ Rules 거부 {retry_count + 1}회째 — 재시도 소진, 폴백 배치로 종료")
            fallback_batches = {npc: _create_fallback_batch(npc) for npc in npcs}
            return {
                "action_batches": fallback_batches,
                "action_batch": _create_fallback_batch(npcs[0]),
                "next": "End",
            }

        print(f"[Supervisor] Rules가 거부함, Dialogue 재시도... (retry={retry_count + 1}/1)")
        return {
            "next": "Dialogue",
            "current_speaker": "Supervisor_Fallback",
            "rules_retry_count": retry_count + 1,
            "natural_context": ("System: Your previous action was rejected by game rules. Respond with speech only."),
        }

    print("[Supervisor] ✅ 파이프라인 정상 완료")
    return {"next": "End"}


# current_speaker → 단계별 라우팅 핸들러. 없는 speaker 는 안전하게 End.
_SPEAKER_ROUTES = {
    "Interface_Input": _route_after_input,
    "Dialogue": _route_after_dialogue,
    "Interface_Output": _route_after_output,
    "Rules": _route_after_rules,
}


def supervisor_node(state: AgentState) -> dict:
    """
    Supervisor / Orchestrator 노드.

    모든 에이전트의 출력을 받아 다음 실행 노드를 결정한다.

    [핵심 원칙]
    - has_error=True이면 즉시 End 숏컷 (보안 위협 또는 치명적 오류)
    - 그 외는 current_speaker를 기반으로 _SPEAKER_ROUTES 단계별 라우팅
    """
    current_speaker = state.get("current_speaker", "")
    has_error = state.get("has_error", False)

    print(f"[Supervisor] 라우팅: {current_speaker} | 에러={has_error}")

    # ── [에러 숏컷] 어떤 노드에서든 에러 발생 시 즉시 종료 ──────
    # 이유: Jailbreak 탐지나 치명적 파싱 오류 시 LLM 호출 낭비 방지
    if has_error:
        error_msg = state.get("error_msg", "Unknown error")
        print(f"[Supervisor] ⚠️  에러 숏컷 → End: {error_msg}")
        return {"next": "End"}

    handler = _SPEAKER_ROUTES.get(current_speaker)
    if handler is None:
        # 알 수 없는 상태 → 안전하게 종료
        print(f"[Supervisor] 알 수 없는 speaker: '{current_speaker}', 종료")
        return {"next": "End"}

    return handler(state)


def should_continue(
    state: AgentState,
) -> Literal["Interface_Input", "Dialogue", "Interface_Output", "Rules", "End"]:
    """
    LangGraph 조건부 엣지 라우터.

    AgentState의 'next' 값으로 다음 노드를 결정한다.
    Supervisor가 설정한 'next' 값을 그대로 반환한다.
    """
    return state.get("next", "End")


def _create_fallback_batch(npc_id: str) -> ActionBatch:
    """
    ActionBatch가 비어있을 때 사용하는 최소 폴백 배치 생성.

    왜 Dialogue 액션인가: 어떤 상황에서도 NPC가 반응하는 모습을 보여야 함.
    빈 배치보다 "혼란스러운 표정"이 UE5에서 더 자연스럽게 처리됨.
    """
    return ActionBatch(
        AgentID=npc_id,
        Mode="Common",
        Actions=[
            GameAction(
                ActionType="Dialogue",
                FacialState="Surprised",
                Parameters={"text": "...", "emotion": "Confused"},
            )
        ],
    )
