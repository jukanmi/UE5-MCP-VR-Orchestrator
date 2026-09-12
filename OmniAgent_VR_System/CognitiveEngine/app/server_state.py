"""
File: server_state.py
Role: 서버 프로세스 수명 동안 살아 있는 런타임 상태 한 묶음.

main.py 의 모듈 전역 5개(월드 상태 캐시·락·활성 UE5 소켓·송신 락·웜업 시각)를 모았다.
디버그 라우터가 같은 소켓으로 UE5 에 쓰려면 이 상태를 공유해야 하는데, 모듈 전역을
`global` 로 흩어 두면 라우터를 다른 파일로 뺄 수 없다. 프로세스당 하나(STATE).
"""

import asyncio
from dataclasses import dataclass, field
from typing import Dict, Optional

from fastapi import WebSocket


@dataclass
class ServerState:
    # state_update 는 NPC 마다 자기 owner_agent_id 기준 payload 를 보낸다. 전역 1개로 두면
    # 마지막에 보고한 NPC 의 perception 이 다른 NPC 의 프롬프트에 주입돼 지식 격리가 깨진다.
    # owner_agent_id(소문자) → payload 로 분리 보관하고, 프롬프트 조립 시 대상 NPC 것만 꺼낸다.
    cached_world_states: Dict[str, dict] = field(default_factory=dict)
    world_state_lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    # 현재 연결된 UE5 LLM 소켓 — 디버그 라우터가 명령·debug_prompt 를 밀어 넣을 때 쓴다.
    active_llm_ws: Optional[WebSocket] = None
    # WS 송신 직렬화 — 메시지별 동시 처리가 같은 소켓에 겹쳐 쓰는 것 방지.
    ws_send_lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    # Stage2 플래너 선제 웜업 스로틀 기준 시각(monotonic).
    last_core_prewarm: float = 0.0

    async def send_to_ue(self, text: str) -> None:
        """활성 UE5 소켓으로 송신(락 직렬화). 소켓 없으면 RuntimeError — 호출부가 HTTP 상태로 번역한다."""
        if self.active_llm_ws is None:
            raise RuntimeError("UE5 미연결")
        async with self.ws_send_lock:
            await self.active_llm_ws.send_text(text)


STATE = ServerState()
