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
import threading
from datetime import datetime
from typing import List, Dict, Optional
from dataclasses import dataclass, asdict
from .llm_factory import get_llm


# ─────────────────────────────────────────────────────────────────────────────
# 토큰 / 메모리 예산 설정
# ─────────────────────────────────────────────────────────────────────────────
# 예산 산정 이력: 구 CHARS_PER_TOKEN=4(영문 기준)는 한국어 토큰을 2~3배 과소평가
# (한국어 ~0.5토큰/자 실측) → 실제로는 예산 대비 훨씬 큰 메모리를 들고 다니다
# 갑자기 요약이 연쇄 발동하는 문제. 한글 가중 추정으로 교정하고, 교정 후 실효
# 볼륨(약 4000자)이 유지되도록 예산을 1000→2000 으로 상향.
MAX_TOKENS_PER_NPC = 2000  # NPC당 최대 토큰 예산 (한글 가중 추정 기준)
SUMMARIZE_THRESHOLD = 0.8  # 80% 도달 시 요약 트리거
# 5→8: 1회 요약당 더 많이 압축 — e2b 호출 횟수와 '요약의 요약' 반복 열화 감소.
ENTRIES_TO_SUMMARIZE = 8  # 1회 요약 대상 최오래된 항목 수
SUMMARIZE_DEBOUNCE_S = 10.0  # 요약 idle 디퍼 — 대화가 이어지는 동안 계속 밀림
# 토큰/문자 비율 (gemma 계열 근사): 한글 0.6, 영문·기호 0.25(=4자/토큰).
HANGUL_TOKEN_RATIO = 0.6
OTHER_TOKEN_RATIO = 0.25


# 메모리 파일 저장 경로
MEMORY_BASE_PATH = "app/agents/knowledge"


@dataclass
class MemoryEntry:
    """NPC와의 단일 대화 교환 기록."""

    timestamp: str
    speaker: str  # "Player" 또는 NPC 이름
    content: str
    is_summary: bool = False

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict) -> "MemoryEntry":
        return cls(**data)

    def estimate_tokens(self) -> int:
        """이 항목의 토큰 수 추정 (정확하지 않아도 됨, 예산 체크용).
        한글은 토큰 밀도가 영문보다 훨씬 높아(≈0.5~0.6토큰/자) 문자수/4 로는
        2~3배 과소평가 — 한글/기타 가중 합산으로 추정."""
        text = f"{self.speaker}: {self.content}"
        # 완성형 음절 + 호환 자모(ㅋㅋ·ㅠㅠ 등) — 자모 누락 시 토큰 과소평가로 요약 트리거 지연.
        hangul = sum(1 for c in text if "가" <= c <= "힣" or "ㄱ" <= c <= "ㅣ")
        return int(hangul * HANGUL_TOKEN_RATIO + (len(text) - hangul) * OTHER_TOKEN_RATIO)


class ConversationMemory:
    """단일 NPC의 대화 메모리를 관리한다."""

    def __init__(self, agent_id: str):
        self.agent_id = agent_id.lower()
        self.entries: List[MemoryEntry] = []
        # add_conversation 이 to_thread 로 멀티스레드 실행되므로 동일 NPC 동시 쓰기/파일저장 보호.
        self.lock = threading.Lock()
        self._summarize_timer: Optional[threading.Timer] = None
        # 요약은 락 밖에서 LLM 을 호출하므로 그 사이 타이머가 또 발화할 수 있다.
        # 중복 요약(같은 항목 이중 압축) 방지용 진행 플래그.
        self._summarizing: bool = False
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
        # 메모리 수정 + 파일 저장을 원자적으로 — 동시 쓰기로 인한 파일 손상/유실 방지.
        with self.lock:
            self.entries.append(entry)
            self._save_to_file()
            # 토큰 예산 초과 시 요약을 idle 후로 디퍼 — 대화 저장 직후 GPU 경합 방지.
            threshold = int(MAX_TOKENS_PER_NPC * SUMMARIZE_THRESHOLD)
            if self.estimate_total_tokens() >= threshold:
                self._schedule_summarize_locked()

    def _schedule_summarize_locked(self):
        """기존 타이머 취소 후 재예약 — lock 보유 중에만 호출할 것.
        대화가 이어지는 동안 타이머가 계속 밀린다(디바운스)."""
        if self._summarize_timer is not None:
            self._summarize_timer.cancel()
        t = threading.Timer(SUMMARIZE_DEBOUNCE_S, self._run_deferred_summarize)
        t.daemon = True  # 프로세스 종료 시 미발화 타이머 유실 허용 — 항목 자체는 이미 저장됨
        self._summarize_timer = t
        t.start()

    def _run_deferred_summarize(self):
        """타이머 콜백 — 워커 스레드에서 실행.

        WHY 락을 통째로 잡지 않는가: 요약은 내부에서 llm.invoke(최대 30초 블로킹)를
        호출한다. 그 구간에 self.lock 을 쥐고 있으면 같은 NPC 의 add_entry 가 전부
        30초씩 대기해 대화 저장이 멈춘다. 락은 짧은 리스트 조작에만 잡는다.
        """
        with self.lock:
            self._summarize_timer = None
            if self._summarizing:
                # 이전 요약이 LLM 대기 중 — 중복 실행하면 같은 항목을 두 번 압축한다.
                return
            self._summarizing = True
        try:
            if self._check_and_summarize():
                with self.lock:
                    self._save_to_file()
        finally:
            with self.lock:
                self._summarizing = False

    def _check_and_summarize(self) -> bool:
        """토큰 예산이 임계치를 초과했는지 확인하고, 임계치 아래로 내려올 때까지 반복 요약한다.

        반환: 항목이 실제로 압축됐으면 True(호출자가 파일 저장). 락 미보유 상태로 호출할 것.
        """
        threshold = int(MAX_TOKENS_PER_NPC * SUMMARIZE_THRESHOLD)
        changed = False

        while True:
            with self.lock:
                current_tokens = self.estimate_total_tokens()
                if current_tokens < threshold:
                    break
                non_summary_count = sum(1 for e in self.entries if not e.is_summary)
                if non_summary_count < ENTRIES_TO_SUMMARIZE:
                    # 요약할 non-summary 항목이 부족하면 더 이상 진행 불가
                    break
            print(f"[Memory] {self.agent_id} 토큰 임계치 도달 ({current_tokens}/{MAX_TOKENS_PER_NPC}), 요약 중...")
            if not self._summarize_oldest_entries():
                # 요약 실패(LLM 에러 등) — 무한 루프 방지를 위해 중단
                break
            changed = True
        return changed

    def _summarize_oldest_entries(self) -> bool:
        """
        오래된 non-summary 항목 + 기존 summary 항목을 모두 묶어 1개 summary로 압축.
        WHY: summary를 누적 추가하면 summary끼리 토큰을 잠식해 무한 요약 루프가 발생함.
             항상 summary가 최대 1개만 유지되도록 기존 summary를 새 요약에 병합한다.

        3단계 구성 — 압축 대상 스냅샷(락) → LLM 호출(락 밖) → 스플라이스(락).
        반환: 실제로 압축했으면 True.
        """
        # 1) 락 하에 압축 대상만 스냅샷 — 짧은 리스트 조작뿐이라 대기가 길지 않다.
        with self.lock:
            non_summary = [e for e in self.entries if not e.is_summary]

            if len(non_summary) < ENTRIES_TO_SUMMARIZE:
                return False

            existing_summaries = [e for e in self.entries if e.is_summary]
            to_compress = non_summary[:ENTRIES_TO_SUMMARIZE]
            targets = existing_summaries + to_compress

        # 이후 lines/LLM 호출은 위에서 뜬 스냅샷만 읽으므로 락 밖에서 수행한다.

        lines = []
        for e in existing_summaries:
            lines.append(f"[Previous summary] {e.content}")
        for e in to_compress:
            lines.append(f"{e.speaker}: {e.content}")

        conversation_text = "\n".join(lines)

        try:
            # num_predict 220: 한국어 3문장 요약 실측 ~100토큰, 기본값 150은 중간 잘림 위험.
            llm = get_llm(model_name="gemma4_e2b", temperature=0.0, num_predict=220)
            # 프롬프트 한국어 필수 — 영어 프롬프트는 e2b 가 영어로 요약해
            # 한국어 대화 컨텍스트에 영어 요약이 주입되던 버그 (2026-07 실측).
            summary_prompt = (
                f"다음 대화를 {self.agent_id}와 Player에 대한 3인칭 서술로 요약하라. "
                "중요한 사실(장소·인물·약속·아이템·위험)과 감정 맥락을 반드시 보존하라. "
                "반드시 한국어로, 최대 3문장.\n\n"
                f"{conversation_text}\n\n요약:"
            )

            response = llm.invoke(summary_prompt)
            summary_text = response.content if hasattr(response, "content") else str(response)
        except Exception as e:
            print(f"[Memory] 요약 실패: {e}")
            return False

        # 3) 락 재획득 후 스플라이스 — LLM 대기 중 추가된 항목은 건드리지 않는다.
        #    MemoryEntry 는 dataclass(eq=True)라 list.remove 는 값이 같은 다른 항목을
        #    지울 수 있다. id() 기준으로 스냅샷한 대상만 정확히 제거한다.
        with self.lock:
            target_ids = {id(e) for e in targets}
            self.entries = [e for e in self.entries if id(e) not in target_ids]
            self.entries.insert(
                0,
                MemoryEntry(
                    timestamp=datetime.now().isoformat(),
                    speaker="[Summary]",
                    content=summary_text.strip(),
                    is_summary=True,
                ),
            )
        print(f"[Memory] {len(existing_summaries)}개 기존 요약 + {len(to_compress)}개 항목 → 1개 요약 완료")
        return True

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
# get_memory 는 to_thread 워커 여러 개에서 동시 호출된다. check-then-set 을 그대로 두면
# 같은 NPC 에 ConversationMemory 인스턴스가 2개 생기고, 각자 자기 self.lock 으로
# 동일한 conversation_memory.json 을 덮어써 대화가 유실된다.
_memory_cache_lock = threading.Lock()


def get_memory(agent_id: str) -> ConversationMemory:
    """NPC ID로 메모리 인스턴스를 가져온다. 없으면 새로 생성."""
    agent_lower = agent_id.lower()
    with _memory_cache_lock:
        memory = _memory_cache.get(agent_lower)
        if memory is None:
            memory = ConversationMemory(agent_id)
            _memory_cache[agent_lower] = memory
    return memory


def add_conversation(agent_id: str, player_input: str, npc_response: str):
    """NPC 메모리에 대화 1라운드(플레이어 + NPC)를 추가한다."""
    memory = get_memory(agent_id)
    memory.add_entry("Player", player_input)
    memory.add_entry(agent_id, npc_response)


def get_conversation_context(agent_id: str, k: int = 5) -> str:
    """NPC의 최근 대화 컨텍스트 문자열 반환."""
    memory = get_memory(agent_id)
    return memory.get_context_string(k)
