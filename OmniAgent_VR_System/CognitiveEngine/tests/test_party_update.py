"""
File: test_party_update.py
Purpose: party_update 수신 → 서버 일행 집합 반영 → 프롬프트 노출 회귀 테스트.

검증 항목:
  - join 은 집합에 추가, 같은 join 반복은 멱등(재연결 재송신 대비)
  - 멤버가 아닌 leave 는 무시, 멤버 leave 는 제거
  - 일행이 있으면 Stage1 프롬프트 끝에 "현재 일행: [...]" 한 줄, 비면 생략
"""

import asyncio
import json
import os
import sys
from unittest.mock import AsyncMock, patch

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import pytest

from app.schemas.actions import DialogueResponse
from app.schemas.envelope import MessageEnvelope
from app.server_state import STATE


@pytest.fixture(autouse=True)
def _clean_party():
    STATE.party_members.clear()
    yield
    STATE.party_members.clear()


def _send(agent_id: str, change: str) -> dict:
    from app.main import _handle_party_update

    env = MessageEnvelope(
        msg_id="test_party_001",
        type="party_update",
        auth_token="omniagent-dev-secret-changeme-before-production",
        payload={"agent_id": agent_id, "change": change},
    )
    return json.loads(_handle_party_update(env))


def test_join_is_idempotent_and_leave_ignores_non_member():
    _send("Elara", "join")
    _send("Elara", "join")
    assert STATE.party_members == {"Elara"}

    _send("James", "leave")  # 멤버 아님 — 무시
    assert STATE.party_members == {"Elara"}

    result = _send("Elara", "leave")
    assert STATE.party_members == set()
    assert not result.get("ActionBatches")  # 통보 전용 — 행동 없음


def _stage1_system_prompt() -> str:
    from app.agents.subgraphs.dialogue import _run_stage1_llm, _Stage1Context

    fmt = dict(
        name="Elara", role="Healer", traits="kind", speech_style="", memory="None", sentiment="Neutral",
        story_goal="None", rag_context="None", chat_history="None", inventory="None", valid_targets="Player",
    )
    ctx = _Stage1Context(fmt, None, "hi", "Player said: hi")
    resp = DialogueResponse(mode="Common", facial="Neutral", speech="안녕", actions=[])
    with patch("app.agents.subgraphs.dialogue.ollama_structured", new_callable=AsyncMock, return_value=resp) as llm:
        asyncio.run(_run_stage1_llm(ctx, "Elara"))
    return llm.await_args.args[0]


def test_party_line_in_prompt_only_when_non_empty():
    assert "현재 일행" not in _stage1_system_prompt()

    _send("James", "join")
    _send("Elara", "join")
    assert _stage1_system_prompt().rstrip().endswith("현재 일행: [Elara, James]")


def test_new_connection_clears_stale_members():
    """WS 만 끊긴 동안의 leave 는 C++ 가 버린다 — 재연결 시 서버 집합을 비워 유령 멤버를 남기지 않는다."""
    from fastapi import WebSocketDisconnect

    from app.main import websocket_llm_endpoint

    class _FakeWS:
        async def accept(self):
            pass  # 가짜 소켓 — 수락 동작은 이 테스트와 무관

        async def receive_text(self):
            raise WebSocketDisconnect()

        async def send_text(self, text):
            pass  # 가짜 소켓 — 송신 내용은 검증하지 않는다

    _send("Ghost", "join")
    asyncio.run(websocket_llm_endpoint(_FakeWS()))
    assert STATE.party_members == set()


# ── M3: 호감도 게이트 · 피격 무시 ─────────────────────────────────────────────


def _resp(*action_types: str, speech: str = "좋아요, 같이 가요!") -> DialogueResponse:
    from app.schemas.actions import DialogueActionItem

    return DialogueResponse(
        mode="Common", facial="Neutral", speech=speech, actions=[DialogueActionItem(type=t) for t in action_types]
    )


def _gate(resp: DialogueResponse, npc: str, score: int) -> DialogueResponse:
    from app.agents.subgraphs.dialogue import _gate_party_actions
    rel = type("Rel", (), {"affinity_score": score})()
    with patch("app.agents.subgraphs.dialogue.db_manager.get_affinity", new_callable=AsyncMock, return_value=rel):
        asyncio.run(_gate_party_actions(resp, npc, "Player"))
    return resp


def test_join_below_threshold_is_refused_with_fixed_line():
    from app.agents.subgraphs.dialogue import _PARTY_REFUSE_LOW_AFFINITY, PARTY_JOIN_AFFINITY
    from app.schemas.actions import DialogueActionItem

    resp = _resp("Follow", "JoinParty", "Scan")
    resp.actions[0].target = "Player"
    resp.facial, resp.tone = "Happy", "cheerfully"
    resp.actions.append(DialogueActionItem(type="Follow", target="Goblin"))
    r = _gate(resp, "Elara", PARTY_JOIN_AFFINITY - 1)
    # 플레이어 대상 Follow·JoinParty 는 제거, 무관 액션(Scan, 다른 대상 Follow)은 유지
    assert [(a.type, a.target) for a in r.actions] == [("Scan", ""), ("Follow", "Goblin")]
    assert r.speech in _PARTY_REFUSE_LOW_AFFINITY
    assert r.facial == "Neutral" and r.tone == ""


def test_affinity_lookup_failure_refuses():
    from app.agents.subgraphs.dialogue import _gate_party_actions

    r = _resp("JoinParty")
    with patch(
        "app.agents.subgraphs.dialogue.db_manager.get_affinity", new_callable=AsyncMock, side_effect=RuntimeError("db")
    ):
        asyncio.run(_gate_party_actions(r, "Elara", "Player"))
    assert r.actions == [] and r.speech != "좋아요, 같이 가요!"


def test_join_at_threshold_passes_untouched():
    from app.agents.subgraphs.dialogue import PARTY_JOIN_AFFINITY

    r = _gate(_resp("JoinParty"), "Elara", PARTY_JOIN_AFFINITY)
    assert [a.type for a in r.actions] == ["JoinParty"]
    assert r.speech == "좋아요, 같이 가요!"


def test_join_when_party_full_is_refused():
    from app.agents.subgraphs.dialogue import _PARTY_REFUSE_FULL, PARTY_MAX_SIZE

    STATE.party_members.update(f"M{i}" for i in range(PARTY_MAX_SIZE))
    r = _gate(_resp("JoinParty"), "Elara", 100)
    assert r.actions == []
    assert r.speech in _PARTY_REFUSE_FULL


def test_member_join_dropped_and_non_member_leave_dropped_speech_kept():
    STATE.party_members.add("Elara")
    r = _gate(_resp("JoinParty"), "Elara", 0)
    assert r.actions == [] and r.speech == "좋아요, 같이 가요!"

    r = _gate(_resp("LeaveParty"), "James", 100)  # 비멤버
    assert r.actions == []

    r = _gate(_resp("LeaveParty"), "Elara", 0)  # 멤버는 통과
    assert [a.type for a in r.actions] == ["LeaveParty"]


def _stage1_prompt_for(transcript: str, npc: str = "Elara") -> str:
    from app.agents.subgraphs.dialogue import _run_stage1_llm, _Stage1Context

    fmt = dict(
        name=npc, role="Healer", traits="kind", speech_style="", memory="None", sentiment="Neutral",
        story_goal="None", rag_context="None", chat_history="None", inventory="None", valid_targets="Player",
    )
    ctx = _Stage1Context(fmt, None, transcript, f"Player said: {transcript}")
    resp = DialogueResponse(mode="Common", facial="Neutral", speech="안녕", actions=[])
    with patch("app.agents.subgraphs.dialogue.ollama_structured", new_callable=AsyncMock, return_value=resp) as llm:
        asyncio.run(_run_stage1_llm(ctx, npc))
    return llm.await_args.args[0]


def test_party_vocab_tail_only_on_invite_dismiss_or_member():
    assert "JoinParty" not in _stage1_system_prompt()  # 잡담 + 비멤버: 꼬리 없음
    for text in ("우리 일행에 합류해 줄래?", "이제 해산하자"):
        prompt = _stage1_prompt_for(text)
        assert "JoinParty" in prompt and "LeaveParty" in prompt and "Stop ONLY" in prompt
    STATE.party_members.add("Elara")
    assert "Stop ONLY" in _stage1_prompt_for("안녕")  # 파티원이면 잡담에도 붙는다
    assert "Stop ONLY" not in _stage1_prompt_for("안녕", npc="James")  # 다른 NPC 는 아님


def _hostile_calls(agent: str, target: str) -> list:
    from app.main import _apply_hostile_affinity

    p = type("P", (), {"danger_score": 0.8, "target_id": target, "sense_type": "Sight"})()
    with patch("app.main.db_manager.get_affinity", new_callable=AsyncMock), patch(
        "app.main.db_manager.update_affinity_sync"
    ) as upd:
        asyncio.run(_apply_hostile_affinity(agent, [p]))
    return upd.call_args_list


def test_hostile_affinity_skipped_only_between_party_members():
    STATE.party_members.update({"Elara", "James"})
    assert _hostile_calls("Elara", "James") == []  # 파티원끼리 — 감점 없음
    assert _hostile_calls("James", "Elara") == []  # 반대 방향도 동일
    assert len(_hostile_calls("Elara", "Player")) == 1  # 플레이어는 집합 밖 — 정상 감점
    assert len(_hostile_calls("Elara", "Goblin")) == 1


# ── 서버 의도 매핑 · 파티원 Stop 보호 · 반복 재생성 ───────────────────────────


@pytest.mark.parametrize(
    "text,expected",
    [
        ("우리 일행에 합류해 줄래", "invite"),
        ("같이 가자", "invite"),
        ("함께 가요", "invite"),
        ("우리 팀에 들어와", "invite"),
        ("일행에서 빠져 줘", "dismiss"),  # '일행' 이 있어도 해산이 우선
        ("이제 해산하자", "dismiss"),
        ("그만 따라와", "dismiss"),
        ("스팀 켜 줘", ""),  # 단어 일부 '팀'
        ("파티션 나눠", ""),
        ("해산물 사 와", ""),
        ("같이 가져와", ""),
        ("나 따라와", ""),  # 따라와 는 초대 아님 — Follow 유지
        ("멈춰", ""),
        # 어미 변형 — 양성
        ("같이 가자고 했잖아", "invite"),
        ("같이 갈까", "invite"),
        ("같이 가줄래", "invite"),
        ("같이 갈래?", "invite"),
        ("일행이 돼 줘", "invite"),
        ("일행으로 받아줘", "invite"),
        ("일행에 들어와", "invite"),
        ("일행 하자", "invite"),
        ("동료가 돼 줘", "invite"),
        ("동료로 삼을게", "invite"),
        ("파티에 들어와", "invite"),
        ("파티 하자", "invite"),
        ("팀 하자", "invite"),
        ("팀이 되자", "invite"),
        # 명사 단독 · 부정 · 이동 요청 — 음성
        ("일행이 어디 있어?", ""),
        ("동료가 필요해", ""),
        ("합류하지 마", ""),
        ("일행 안 해", ""),
        ("같이 가기 싫어", ""),
        ("같이 가자, 저 상점으로", ""),
        ("함께 가요 마을로", ""),
        ("같이 가자 시장에", ""),
        ("같이 가자 시장까지", ""),
        ("해산하지 마", ""),
        ("헤어지기 전에 인사하자", ""),
        # 해산 청유·명령·의지형 — 양성
        ("헤어지자", "dismiss"),
        ("헤어져", "dismiss"),
        ("헤어질래", "dismiss"),
        ("헤어지죠", "dismiss"),
    ],
)
def test_detect_party_intent_keywords(text, expected):
    from app.agents.party_intent import detect_party_intent

    assert detect_party_intent(text) == expected


def _intent(resp: DialogueResponse, npc: str, transcript: str, member: bool) -> DialogueResponse:
    from app.agents.subgraphs.dialogue import _apply_party_intent

    if member:
        STATE.party_members.add(npc)
    _apply_party_intent(resp, npc, transcript, "Player")
    return resp


def _follow_player() -> DialogueResponse:
    from app.schemas.actions import DialogueActionItem

    r = _resp()
    r.actions = [DialogueActionItem(type="Follow", target="Player")]
    return r


def test_invite_follow_becomes_join_then_hits_affinity_gate():
    r = _intent(_follow_player(), "Elara", "같이 가자", member=False)
    assert [a.type for a in r.actions] == ["JoinParty"]
    r = _gate(r, "Elara", 0)  # 치환된 JoinParty 가 호감도 게이트에서 거절된다
    assert r.actions == []

    r = _intent(_follow_player(), "James", "나 따라와", member=False)  # 초대 아님 — Follow 유지
    assert [a.type for a in r.actions] == ["Follow"]
    assert _intent(_resp(), "Moca", "같이 가자", member=False).actions == []  # 액션 없으면 추가하지 않는다


def test_dismiss_member_stop_or_follow_becomes_leave():
    assert [a.type for a in _intent(_resp("Stop"), "Elara", "해산하자", member=True).actions] == ["LeaveParty"]
    assert [a.type for a in _intent(_follow_player(), "James", "그만 따라와", member=True).actions] == ["LeaveParty"]
    # 비멤버는 해산 대상이 아니다 — Stop 그대로
    assert [a.type for a in _intent(_resp("Stop"), "Moca", "해산하자", member=False).actions] == ["Stop"]


def test_member_stop_without_dismiss_keyword_becomes_idle():
    assert [a.type for a in _intent(_resp("Stop"), "Elara", "멈춰", member=True).actions] == ["Idle"]
    assert [a.type for a in _intent(_resp("Stop"), "Moca", "멈춰", member=False).actions] == ["Stop"]


def test_invite_hint_in_natural_context():
    from app.agents.interface_input import _PARTY_HINTS

    assert "JoinParty" in _PARTY_HINTS["invite"] and "LeaveParty" in _PARTY_HINTS["dismiss"]


def _chat_ctx(chat_history: str):
    from app.agents.subgraphs.dialogue import _Stage1Context

    fmt = dict(
        name="Elara", role="Healer", traits="kind", speech_style="", memory="None", sentiment="Neutral",
        story_goal="None", rag_context="None", chat_history=chat_history, inventory="None", valid_targets="Player",
    )
    return _Stage1Context(fmt, None, "hi", "Player said: hi")


def _run_with_replies(chat_history: str, *speeches: str):
    from app.agents.subgraphs.dialogue import _run_stage1_llm

    replies = [DialogueResponse(mode="Common", facial="Neutral", speech=sp, actions=[]) for sp in speeches]
    with patch("app.agents.subgraphs.dialogue.ollama_structured", new_callable=AsyncMock, side_effect=replies) as llm:
        resp, _ = asyncio.run(_run_stage1_llm(_chat_ctx(chat_history), "Elara"))
    return resp, llm


def test_repeated_line_triggers_single_retry_with_higher_temperature():
    history = "Player: 안녕\nElara: 그래, 아직도 남아있어.\nEvent: 무언가 보였다"
    resp, llm = _run_with_replies(history, "그래 아직도 남아있어!", "어서 와요, 오랜만이네요.")
    assert llm.await_count == 2 and resp.speech == "어서 와요, 오랜만이네요."
    first, second = llm.await_args_list
    assert second.kwargs["temperature"] > first.kwargs["temperature"]
    assert "직전 문장을 반복하지 말 것" in second.args[1]


def test_retry_happens_once_and_nonrepeat_or_short_skips():
    history = "Player: 안녕\nElara: 그래, 아직도 남아있어."
    resp, llm = _run_with_replies(history, "그래, 아직도 남아있어.", "그래, 아직도 남아있어.")
    assert llm.await_count == 2 and resp.speech == "그래, 아직도 남아있어."  # 재호출도 반복이면 그대로
    _, llm = _run_with_replies(history, "처음 보는 얼굴이군요.")
    assert llm.await_count == 1
    _, llm = _run_with_replies("Elara: 네.", "네.")  # 너무 짧은 대사는 검사 제외
    assert llm.await_count == 1


def test_context_string_splits_events_from_dialogue():
    from app.utils.memory_manager import ConversationMemory, MemoryEntry

    m = ConversationMemory.__new__(ConversationMemory)  # 파일 I/O 없이 entries 만 채운다
    rows = [("Player", "안녕"), ("Elara", "어서 와요"), ("Event", "좀비가 보였다"), ("Player", "뭐야?"),
            ("Event", "플레이어가 피격당했다"), ("Elara", "조심해요"), ("Event", "문이 열렸다")]
    m.entries = [MemoryEntry("t", sp, c) for sp, c in rows]
    out = m.get_context_string(5).splitlines()
    # 헤더 포함 전체 5줄(k) 상한 — 대화 2줄 + 헤더 + Event 2줄
    assert out == ["Player: 뭐야?", "Elara: 조심해요",
                   "Recent events:", "- 플레이어가 피격당했다", "- 문이 열렸다"]
    assert len(out) <= 5 and "좀비가 보였다" not in "\n".join(out)  # 2줄 상한 밖의 오래된 Event
    m.entries = [MemoryEntry("t", "Player", "안녕"), MemoryEntry("t", "Elara", "네")]
    assert m.get_context_string(5) == "Player: 안녕\nElara: 네"  # Event 없으면 종전과 동일


def _hint_context(transcript: str) -> str:
    from app.agents.interface_input import interface_input_node

    state = {"vr_context": {"player_id": "P", "voice_transcript": transcript, "target_npc_id": "Elara", "timestamp": 0.0},
             "target_npc": "Elara", "target_npcs": ["Elara"]}
    return interface_input_node(state)["natural_context"]


def test_natural_context_hint_only_on_real_invite():
    assert "Hint:" in _hint_context("같이 가자") and "JoinParty" in _hint_context("같이 가자")
    assert "LeaveParty" in _hint_context("이제 헤어지자")
    assert "Hint:" not in _hint_context("안녕")
    assert "Hint:" not in _hint_context("같이 가자, 저 상점으로")  # 이동 요청


def test_repeat_retry_skips_fallback_and_keeps_original_on_failure():
    history = "Player: 안녕\nElara: 그래, 아직도 남아있어."
    from app.agents.subgraphs.dialogue import _run_stage1_llm

    first = DialogueResponse(mode="Common", facial="Neutral", speech="그래, 아직도 남아있어.", actions=[])
    # 재생성 호출이 예외 — 폴백 없이 즉시 포기(총 2회), 원 응답 유지
    with patch("app.agents.subgraphs.dialogue.ollama_structured", new_callable=AsyncMock,
               side_effect=[first, RuntimeError("boom")]) as llm:
        resp, _ = asyncio.run(_run_stage1_llm(_chat_ctx(history), "Elara", {"stage": "stage1"}))
    assert llm.await_count == 2 and resp.speech == first.speech
    retry_log = llm.await_args_list[1].kwargs["log_extra"]
    assert retry_log["repeat_retry"] is True and retry_log["repeat_discarded_speech"] == first.speech


# 실측(Guard 에게 "안녕" 3회): 문장 하나를 매번 그대로 붙이는 복사 — 전체 유사도 0.765 라 줄 단위로는 못 잡는다.
_GUARD_LINES = [
    "반갑다. 성문은 여전히 이곳에 남아있군.",
    "무슨 일이지? 성문은 여전히 이곳에 남아있군.",
    "어서 와. 성문은 여전히 이곳에 남아있군.",
]


def test_sentence_level_repeat_detected_on_measured_sequence():
    from app.agents.subgraphs.dialogue import _repeated_line

    history = "Player: 안녕\n" + f"Guard: {_GUARD_LINES[0]}"
    assert _repeated_line(_GUARD_LINES[1], history, "Guard") == "성문은 여전히 이곳에 남아있군"
    history += "\nPlayer: 안녕\n" + f"Guard: {_GUARD_LINES[1]}"
    assert _repeated_line(_GUARD_LINES[2], history, "Guard") == "성문은 여전히 이곳에 남아있군"
    assert _repeated_line(_GUARD_LINES[0], "", "Guard") == ""  # 기록 없음


def test_short_overlapping_sentences_are_not_repeat():
    from app.agents.subgraphs.dialogue import _repeated_line

    history = "Player: 안녕\nGuard: 안녕. 알겠어. 무슨 일로 왔지?"
    assert _repeated_line("안녕. 알겠어. 오늘은 날씨가 좋군.", history, "Guard") == ""


def test_retry_prompt_names_the_repeated_sentence():
    history = "Player: 안녕\nElara: " + _GUARD_LINES[0]
    resp, llm = _run_with_replies(history, _GUARD_LINES[1], "오랜만이군, 잘 지냈나?")
    assert llm.await_count == 2 and resp.speech == "오랜만이군, 잘 지냈나?"
    assert '"성문은 여전히 이곳에 남아있군"' in llm.await_args_list[1].args[1]


def test_repeated_sentence_stripped_when_retry_also_repeats():
    history = "Player: 안녕\nGuard: " + _GUARD_LINES[0]
    from app.agents.subgraphs.dialogue import _run_stage1_llm

    replies = [DialogueResponse(mode="Common", facial="Neutral", speech=sp, actions=[])
               for sp in (_GUARD_LINES[1], "무슨 일로 온 거지? 성문은 여전히 이곳에 남아있군.")]
    ctx = _chat_ctx(history)
    ctx.fmt_kwargs["name"] = "Guard"
    with patch("app.agents.subgraphs.dialogue.ollama_structured", new_callable=AsyncMock, side_effect=replies) as llm:
        resp, _ = asyncio.run(_run_stage1_llm(ctx, "Guard"))
    assert llm.await_count == 2  # 재생성은 1회뿐
    assert resp.speech == "무슨 일로 온 거지?"  # 반복 문장만 제거


def test_strip_keeps_speech_when_remainder_too_short():
    from app.agents.subgraphs.dialogue import _strip_repeated_sentences

    history = "Player: 안녕\nGuard: " + _GUARD_LINES[0]
    speech = "응. 성문은 여전히 이곳에 남아있군."  # 제거하면 "응." 만 남아 빈 대사에 가깝다
    assert _strip_repeated_sentences(speech, history, "Guard") == (speech, [])
    # 원 응답이 반복이고 재생성이 실패해도 같은 규칙으로 제거된다
    rest, removed = _strip_repeated_sentences("무슨 일이지? 성문은 여전히 이곳에 남아있군.", history, "Guard")
    assert rest == "무슨 일이지?" and removed == ["성문은 여전히 이곳에 남아있군."]
