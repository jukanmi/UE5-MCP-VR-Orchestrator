"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: memory_manager.py                                                     ║
║ Role: NPC 대화 메모리 관리                                                   ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ 핵심 역할:                                                                   ║
║   1. NPC별 대화 히스토리 저장 및 로컬 JSON 파일로 영속화                    ║
║   2. 토큰 예산 초과 시 LLM 요약으로 오래된 메모리 압축                      ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import os
import json
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
