"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: supervisor.py                                                         ║
║ Role: ORCHESTRATOR (파이프라인 오케스트레이터)                               ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ 핵심 역할 (변경 금지):                                                       ║
║   에이전트 간 라우팅 결정. 에러·폴백 처리로 파이프라인 완료를 보장한다.      ║
║                                                                              ║
║ 라우팅 흐름:                                                                 ║
║   [Error/Empty] → 즉시 End (숏컷, LLM 호출 없음)                           ║
║   Interface_Input → Dialogue                                                ║
║   Dialogue → Interface_Output                                               ║
║   Interface_Output → Rules                                                  ║
║   Rules → End (정상) 또는 Dialogue (거부 재시도)                            ║
║                                                                              ║
║ 에러 숏컷 원칙 (청사진 요구사항):                                            ║
║   - has_error=True → 어떤 노드에서든 즉시 End로 우회                        ║
║   - target_npcs가 비어있어서 처리 대상 없음 → End로 우회                   ║
║   → 불필요한 LLM 호출을 제거해 토큰·지연 비용 절감                          ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
from typing import Literal
from .state import AgentState
from ..schemas.actions import ActionBatch, NPCAction


def supervisor_node(state: AgentState) -> dict:
    """
    Supervisor / Orchestrator 노드.

    모든 에이전트의 출력을 받아 다음 실행 노드를 결정한다.

    [핵심 원칙]
    - has_error=True이면 즉시 End 숏컷 (보안 위협 또는 치명적 오류)
    - target_npcs 비어있으면 즉시 End 숏컷 (처리 대상 없음)
    - 그 외는 current_speaker를 기반으로 순차 라우팅
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

    # ── 1단계: Interface_Input 이후 → Dialogue로 ───────────────
    if current_speaker == "Interface_Input":
        natural_context = state.get("natural_context", "")

        if not natural_context:
            print("[Supervisor] ⚠️  natural_context 비어있음, 계속 진행")

        # target_npcs 비어있으면 처리 대상 없음 → 즉시 종료
        target_npcs = state.get("target_npcs", [])
        if len(target_npcs) == 0 and not state.get("target_npc"):
            print("[Supervisor] 대상 NPC 없음, 숏컷 → End")
            return {"next": "End"}

        return {
            "next": "Dialogue",
            "current_speaker": "Supervisor",
        }

    # ── 2단계: Dialogue 이후 → Interface_Output으로 ────────────
    if current_speaker == "Dialogue":
        raw_response = state.get("raw_response", "")

        if not raw_response:
            print("[Supervisor] ⚠️  raw_response 비어있음, 폴백 주입")
            return {
                "raw_response": '"..." (confused)',
                "next": "Interface_Output",
                "current_speaker": "Supervisor",
            }

        return {
            "next": "Interface_Output",
            "current_speaker": "Supervisor",
        }

    # ── 3단계: Interface_Output 이후 → Rules로 ─────────────────
    if current_speaker == "Interface_Output":
        action_batch = state.get("action_batch")

        if not action_batch or not action_batch.actions:
            print("[Supervisor] ⚠️  ActionBatch 비어있음, 폴백 배치 생성")
            npc_id = state.get("target_npc", "Elara")
            return {
                "action_batch": _create_fallback_batch(npc_id),
                "next": "Rules",
                "current_speaker": "Supervisor",
            }

        return {
            "next": "Rules",
            "current_speaker": "Supervisor",
        }

    # ── 4단계: Rules 이후 → End(정상) 또는 Dialogue(거부) ──────
    if current_speaker == "Rules":
        action_batch = state.get("action_batch")

        # ActionBatch 거부 판정: reasoning에 "REJECTED" 포함 또는 액션 없음
        is_rejected = (
            not action_batch
            or "REJECTED" in (action_batch.reasoning or "")
            or not action_batch.actions
        )

        if is_rejected:
            print("[Supervisor] Rules가 거부함, Dialogue 재시도...")
            return {
                "next": "Dialogue",
                "current_speaker": "Supervisor_Fallback",
                "natural_context": (
                    "System: Your previous action was rejected by game rules. "
                    "Respond with speech only."
                ),
            }

        print("[Supervisor] ✅ 파이프라인 정상 완료")
        return {"next": "End"}

    # ── 기본: 알 수 없는 상태 → 안전하게 종료 ───────────────────
    print(f"[Supervisor] 알 수 없는 speaker: '{current_speaker}', 종료")
    return {"next": "End"}


def should_continue(state: AgentState) -> Literal[
    "Interface_Input", "Dialogue", "Interface_Output", "Rules", "End"
]:
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
        agent_id=npc_id,
        actions=[NPCAction(
            action_category="Common",
            action_type="Dialogue",
            executor_npc_id=npc_id,
            emotion="Confused",
            parameters={"text": "...", "emotion": "Confused"},
        )],
        reasoning="Supervisor Fallback: 빈 배치 수신"
    )
