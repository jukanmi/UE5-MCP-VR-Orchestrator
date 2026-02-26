"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: memory_manager.py                                                     ║
║ Role: NPC 대화 메모리 관리 + 비동기 TTL 청소                                ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ 핵심 역할:                                                                   ║
║   1. NPC별 대화 히스토리 저장 및 로컬 JSON 파일로 영속화                    ║
║   2. 토큰 예산 초과 시 LLM 요약으로 오래된 메모리 압축                      ║
║   3. [신규] 5분 주기 비동기 TTL 청소 → 메인 파이프라인 비블로킹             ║
║                                                                              ║
║ 청사진 요구사항 (System_Architecture_Python_Backend.md §3):                 ║
║   - event_history 데이터가 5분 초과 시 백그라운드 Worker로 압축·요약        ║
║   - asyncio.create_task 사용으로 메인 파이프라인 프레임 지연 없음            ║
║   - MAX_EVENTS=10 Sliding Window로 메모리·컨텍스트 한도 초과 방지           ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import os
import json
import asyncio
import time
from datetime import datetime
from typing import List, Dict, Optional
from dataclasses import dataclass, asdict
from .llm_factory import get_llm


# ─────────────────────────────────────────────────────────────────────────────
# 토큰 / 메모리 예산 설정
# ─────────────────────────────────────────────────────────────────────────────
MAX_TOKENS_PER_NPC = 1000       # NPC당 최대 토큰 예산 (RAG 메모리 500~1000)
SUMMARIZE_THRESHOLD = 0.8       # 80% 도달 시 요약 트리거
ENTRIES_TO_SUMMARIZE = 5        # 1회 요약 대상 최오래된 항목 수
CHARS_PER_TOKEN = 4             # 토큰 추정 단위 (conservative)

# event_history TTL 설정 (청사진 §3)
TTL_SECONDS = 300               # 5분 = 300초
TTL_CLEANUP_INTERVAL = 60       # 60초마다 TTL 청소 Worker 동작

# 메모리 파일 저장 경로
MEMORY_BASE_PATH = "app/agents/knowledge"


@dataclass
class MemoryEntry:
    """NPC와의 단일 대화 교환 기록."""
    timestamp: str
    speaker: str        # "Player" 또는 NPC 이름
    content: str
    is_summary: bool = False

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict) -> "MemoryEntry":
        return cls(**data)

    def estimate_tokens(self) -> int:
        """이 항목의 토큰 수 추정 (정확하지 않아도 됨, 예산 체크용)."""
        text = f"{self.speaker}: {self.content}"
        return len(text) // CHARS_PER_TOKEN


class ConversationMemory:
    """단일 NPC의 대화 메모리를 관리한다."""

    def __init__(self, agent_id: str):
        self.agent_id = agent_id.lower()
        self.entries: List[MemoryEntry] = []
        self._load_from_file()

    @property
    def memory_file_path(self) -> str:
        """메모리 JSON 파일 경로."""
        return os.path.join(MEMORY_BASE_PATH, self.agent_id, "conversation_memory.json")

    def _load_from_file(self):
        """저장된 메모리를 JSON 파일에서 불러온다."""
        if os.path.exists(self.memory_file_path):
            try:
                with open(self.memory_file_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    self.entries = [MemoryEntry.from_dict(e) for e in data.get("entries", [])]
                print(f"[Memory] {self.agent_id}: {len(self.entries)}개 항목 로드")
            except Exception as e:
                print(f"[Memory] {self.agent_id} 로드 실패: {e}")
                self.entries = []
        else:
            print(f"[Memory] {self.agent_id}: 기존 메모리 없음")

    def _save_to_file(self):
        """현재 메모리를 JSON 파일에 저장한다."""
        os.makedirs(os.path.dirname(self.memory_file_path), exist_ok=True)
        try:
            data = {
                "agent_id": self.agent_id,
                "last_updated": datetime.now().isoformat(),
                "entries": [e.to_dict() for e in self.entries],
            }
            with open(self.memory_file_path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
        except Exception as e:
            print(f"[Memory] {self.agent_id} 저장 실패: {e}")

    def estimate_total_tokens(self) -> int:
        """전체 메모리 사용 토큰 추정값 반환."""
        return sum(e.estimate_tokens() for e in self.entries)

    def add_entry(self, speaker: str, content: str):
        """
        새 대화 항목을 추가한다.

        이후 토큰 예산 검사 → 필요시 자동 요약. 파일 저장도 수행.
        """
        entry = MemoryEntry(
            timestamp=datetime.now().isoformat(),
            speaker=speaker,
            content=content,
            is_summary=False,
        )
        self.entries.append(entry)

        # 토큰 예산 초과 시 요약으로 압축
        self._check_and_summarize()
        self._save_to_file()

    def _check_and_summarize(self):
        """토큰 예산이 임계치를 초과했는지 확인하고 요약 처리한다."""
        current_tokens = self.estimate_total_tokens()
        threshold = int(MAX_TOKENS_PER_NPC * SUMMARIZE_THRESHOLD)

        if current_tokens >= threshold:
            print(f"[Memory] {self.agent_id} 토큰 임계치 도달 ({current_tokens}/{MAX_TOKENS_PER_NPC}), 요약 중...")
            self._summarize_oldest_entries()

    def _summarize_oldest_entries(self):
        """오래된 non-summary 항목들을 LLM으로 요약해 1개 항목으로 압축한다."""
        non_summary = [e for e in self.entries if not e.is_summary]

        if len(non_summary) < ENTRIES_TO_SUMMARIZE:
            return

        to_summarize = non_summary[:ENTRIES_TO_SUMMARIZE]
        conversation_text = "\n".join(f"{e.speaker}: {e.content}" for e in to_summarize)

        try:
            llm = get_llm(temperature=0.3)
            summary_prompt = (
                f"Summarize the following conversation between Player and {self.agent_id} "
                "into a brief third-person narrative. Keep important facts and emotional context. "
                "Maximum 2-3 sentences.\n\n"
                f"Conversation:\n{conversation_text}\n\nSummary:"
            )

            response = llm.invoke(summary_prompt)
            summary_text = response.content if hasattr(response, "content") else str(response)

            # 오래된 항목 제거 후 요약 항목 삽입
            for entry in to_summarize:
                self.entries.remove(entry)

            self.entries.insert(0, MemoryEntry(
                timestamp=datetime.now().isoformat(),
                speaker="[Summary]",
                content=summary_text.strip(),
                is_summary=True,
            ))
            print(f"[Memory] {len(to_summarize)}개 항목 → 1개 요약 완료")
        except Exception as e:
            print(f"[Memory] 요약 실패: {e}")

    def get_recent_entries(self, k: int = 5) -> List[MemoryEntry]:
        """최근 k개 항목 반환."""
        return self.entries[-k:] if self.entries else []

    def get_context_string(self, k: int = 5) -> str:
        """LLM 프롬프트에 삽입할 최근 대화 컨텍스트 문자열 반환."""
        recent = self.get_recent_entries(k)
        if not recent:
            return ""

        lines = []
        for entry in recent:
            if entry.is_summary:
                lines.append(f"[Past events] {entry.content}")
            else:
                lines.append(f"{entry.speaker}: {entry.content}")

        return "\n".join(lines)


# ─────────────────────────────────────────────────────────────────────────────
# 글로벌 메모리 캐시 (NPC당 1개 인스턴스)
# ─────────────────────────────────────────────────────────────────────────────
_memory_cache: Dict[str, ConversationMemory] = {}

# event_history TTL 추적용: {msg_id: unix_timestamp}
_event_timestamps: Dict[str, float] = {}


def get_memory(agent_id: str) -> ConversationMemory:
    """NPC ID로 메모리 인스턴스를 가져온다. 없으면 새로 생성."""
    agent_lower = agent_id.lower()
    if agent_lower not in _memory_cache:
        _memory_cache[agent_lower] = ConversationMemory(agent_id)
    return _memory_cache[agent_lower]


def add_conversation(agent_id: str, player_input: str, npc_response: str):
    """NPC 메모리에 대화 1라운드(플레이어 + NPC)를 추가한다."""
    memory = get_memory(agent_id)
    memory.add_entry("Player", player_input)
    memory.add_entry(agent_id, npc_response)


def get_conversation_context(agent_id: str, k: int = 5) -> str:
    """NPC의 최근 대화 컨텍스트 문자열 반환."""
    memory = get_memory(agent_id)
    return memory.get_context_string(k)


def clear_memory_cache():
    """메모리 캐시 전체 초기화 (테스트용)."""
    global _memory_cache
    _memory_cache.clear()


# ─────────────────────────────────────────────────────────────────────────────
# [비동기 TTL 청소] event_history 5분 초과 항목 자동 제거
# 청사진 §3 요구사항: 메인 파이프라인을 블로킹하지 않는 비동기 방식
# ─────────────────────────────────────────────────────────────────────────────

def register_event(msg_id: str):
    """
    event_history에 새 이벤트가 추가될 때 타임스탬프를 등록한다.
    TTL Worker가 이 시간을 기준으로 만료 여부를 판정한다.
    """
    _event_timestamps[msg_id] = time.time()


def get_expired_event_ids(event_history: list) -> list:
    """
    event_history에서 TTL(5분)이 만료된 msg_id 목록을 반환한다.

    왜 별도 함수인가: 메인 파이프라인(동기)과 TTL Worker(비동기)가
    모두 사용하므로 로직을 한 곳에 집중.
    """
    now = time.time()
    expired_ids = []

    for event in event_history:
        msg_id = event.get("msg_id", "")
        registered_at = _event_timestamps.get(msg_id, 0)

        # 등록된 시간이 없거나 TTL 초과 시 만료 처리
        if registered_at == 0 or (now - registered_at) > TTL_SECONDS:
            expired_ids.append(msg_id)

    return expired_ids


def prune_event_history(event_history: list, max_events: int = 10) -> list:
    """
    event_history에서 만료된 항목을 제거하고 Sliding Window 크기를 제한한다.

    처리 순서:
    1. TTL 만료 항목 제거
    2. MAX_EVENT_HISTORY 초과 시 오래된 항목 제거 (OOM 방어)
    """
    expired_ids = set(get_expired_event_ids(event_history))
    pruned = [e for e in event_history if e.get("msg_id") not in expired_ids]

    if len(expired_ids) > 0:
        print(f"[TTL Worker] {len(expired_ids)}개 만료 이벤트 제거")

    # Sliding Window: MAX 초과분 오래된 것부터 제거
    if len(pruned) > max_events:
        removed = len(pruned) - max_events
        pruned = pruned[-max_events:]
        print(f"[TTL Worker] Sliding Window: {removed}개 초과 항목 제거")

    return pruned


async def ttl_cleanup_worker(get_current_state_fn, update_state_fn):
    """
    비동기 TTL 청소 백그라운드 Worker.

    매 60초마다 event_history를 체크하여 만료 항목을 제거한다.
    asyncio.create_task()로 실행되어 메인 루프를 블로킹하지 않는다.

    파라미터:
        get_current_state_fn: 현재 event_history를 반환하는 콜백
        update_state_fn: 정리된 event_history를 저장하는 콜백
    """
    print("[TTL Worker] 비동기 TTL 청소 Worker 시작 (주기: 60초)")

    while True:
        try:
            await asyncio.sleep(TTL_CLEANUP_INTERVAL)

            current_history = get_current_state_fn()
            if not current_history:
                continue

            cleaned_history = prune_event_history(current_history)

            if len(cleaned_history) < len(current_history):
                update_state_fn(cleaned_history)
                print(f"[TTL Worker] 청소 완료: {len(current_history)} → {len(cleaned_history)}개")

        except asyncio.CancelledError:
            # FastAPI 종료 시 정상 취소
            print("[TTL Worker] 종료됨")
            break
        except Exception as e:
            # Worker 오류가 메인 서버를 죽이지 않도록 예외 캐치
            print(f"[TTL Worker] 예외 발생 (무시하고 계속): {e}")
