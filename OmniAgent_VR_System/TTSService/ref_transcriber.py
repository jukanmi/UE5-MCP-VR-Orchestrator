"""
File: OmniAgent_VR_System/TTSService/ref_transcriber.py
Role: base_voices/<ref>.wav 자동 전사 → voice_map.yaml 의 ref_texts 빈칸 자동 채움.

WHY:
  - CosyVoice2 zero_shot 은 (prompt_wav + prompt_text) 쌍 필요. prompt_text 미작성 시
    inference_cross_lingual 폴백(품질·속도 저하). WAV 만 넣어두면 자동 전사로 빈칸을
    메워 zero_shot 을 활성화.
  - 정책: "빈칸/누락만 채움". 사용자가 yaml 에 직접 적은 전사는 절대 덮어쓰지 않음
    (force=True 일 때만 전체 재전사). → 수동 수정분 영구 보존.
  - 주석/TODO/구조 보존 위해 yaml 라이브러리 round-trip(주석 소실) 대신 부분 텍스트 치환.

NOTES:
  - whisper 는 openai-whisper (WSL venv 기설치 — CosyVoice frontend 의존). GPU 사용.
  - 모델 로드 비용 크므로 채울 대상이 1개 이상일 때만 lazy 로드.
  - 환경변수:
      CV_WHISPER_MODEL  (기본 large-v3)   전사 정확도 우선. medium 등으로 낮춰 VRAM 절감 가능.
      CV_WHISPER_LANG   (기본 ko)         빈 문자열이면 whisper 자동 감지.
      CV_AUTO_TRANSCRIBE(기본 1)          서버 기동 시 자동 스캔 on/off (server.py 에서 읽음).
"""

from __future__ import annotations

import json
import logging
import os
import re
import threading
from pathlib import Path
from typing import Optional

import yaml

logger = logging.getLogger("tts")

_BASE_DIR = Path(__file__).parent
MAP_PATH = _BASE_DIR / "voice_map.yaml"
BASE_VOICES_DIR = Path(os.getenv("CV_BASE_VOICES_DIR", str(_BASE_DIR / "base_voices")))

WHISPER_MODEL = os.getenv("CV_WHISPER_MODEL", "large-v3")
WHISPER_LANG = os.getenv("CV_WHISPER_LANG", "ko")  # "" → 자동 감지

_whisper_lock = threading.Lock()  # _load_whisper 이중 로드(VRAM 누수) 방지
_apply_lock = threading.Lock()  # apply_fills write-write 레이스(bg task vs API) 방지

_whisper = None  # lazy 로드된 whisper 모델


# ─────────────────────────────────────────────────────────────────────────────
# whisper
# ─────────────────────────────────────────────────────────────────────────────
def _load_whisper():
    global _whisper
    if _whisper is not None:
        return _whisper
    with _whisper_lock:
        if _whisper is not None:  # double-checked locking — 대기 중 다른 스레드가 먼저 로드
            return _whisper
        import whisper  # openai-whisper

        logger.info(f"[TTS][transcribe] whisper 로드: {WHISPER_MODEL}")
        _whisper = whisper.load_model(WHISPER_MODEL)
    return _whisper


def transcribe_one(wav_path: Path) -> str:
    """단일 WAV → 전사 문자열(공백 정리). 실패 시 RuntimeError."""
    model = _load_whisper()
    is_cpu = hasattr(model, "device") and model.device.type == "cpu"
    kwargs = {"fp16": not is_cpu}
    if WHISPER_LANG:
        kwargs["language"] = WHISPER_LANG
    result = model.transcribe(str(wav_path), **kwargs)
    text = " ".join(str(result.get("text", "")).split()).strip()
    if not text:
        raise RuntimeError("전사 결과 비어 있음")
    return text


# ─────────────────────────────────────────────────────────────────────────────
# voice_map 읽기 / 빈칸 탐지
# ─────────────────────────────────────────────────────────────────────────────
def _read_ref_texts(map_path: Path) -> dict:
    try:
        with map_path.open("r", encoding="utf-8") as f:
            data = yaml.safe_load(f) or {}
        return data.get("ref_texts") or {}
    except Exception as e:
        logger.warning(f"[TTS][transcribe] voice_map 읽기 실패: {e}")
        return {}


def _is_empty(val) -> bool:
    return val is None or (isinstance(val, str) and not val.strip())


_REF_EXTS = {".wav", ".mp3", ".flac", ".ogg", ".m4a"}  # server.py REF_EXTS 와 동기화


def find_fillable(base_voices_dir: Path, map_path: Path, force: bool = False) -> list[str]:
    """전사 대상 stem 목록.

    - base_voices/<stem>.{wav,mp3,flac,ogg,m4a} 존재 (server.py REF_EXTS 와 동일)
    - force=False: ref_texts[stem] 가 빈칸/누락인 것만 (수동 작성분 보존)
    - force=True : 오디오 파일 있는 전체 stem
    """
    ref_texts = _read_ref_texts(map_path)
    stems = sorted({p.stem for p in base_voices_dir.glob("*") if p.suffix.lower() in _REF_EXTS})
    if force:
        return stems
    return [s for s in stems if _is_empty(ref_texts.get(s))]


# ─────────────────────────────────────────────────────────────────────────────
# 부분 텍스트 치환 (주석 보존)
# ─────────────────────────────────────────────────────────────────────────────
def _yaml_quote(text: str) -> str:
    # JSON 더블쿼트 스칼라는 YAML 더블쿼트 스칼라의 부분집합 — 안전하게 재사용.
    return json.dumps(text, ensure_ascii=False)


def _block_bounds(lines: list[str]) -> tuple[int, int]:
    """ref_texts: 블록의 [헤더 다음 줄, 블록 끝(배타)) 인덱스. 없으면 (-1,-1)."""
    start = -1
    for i, ln in enumerate(lines):
        if re.match(r"^ref_texts:\s*(?:#.*)?$", ln):
            start = i
            break
    if start == -1:
        return -1, -1
    end = len(lines)
    for j in range(start + 1, len(lines)):
        ln = lines[j]
        if not ln.strip() or ln.strip().startswith("#"):
            continue
        # 들여쓰기 없는(최상위) 줄 = 블록 종료
        if not ln.startswith((" ", "\t")):
            end = j
            break
    return start + 1, end


def apply_fills(map_path: Path, fills: dict[str, str]) -> list[str]:
    """fills(stem→전사)를 voice_map.yaml 에 반영. 기존 키는 값 치환, 누락 키는 블록 끝에 삽입.

    반환: 실제 기록한 stem 목록.
    _apply_lock 으로 bg task ↔ API 동시 호출 직렬화 + tmp→rename 원자적 쓰기.
    """
    if not fills:
        return []
    with _apply_lock:
        raw = map_path.read_text(encoding="utf-8")
        nl = "\r\n" if "\r\n" in raw else "\n"
        lines = raw.split(nl)

        blk_start, blk_end = _block_bounds(lines)
        if blk_start == -1:
            logger.warning("[TTS][transcribe] ref_texts: 블록 없음 — 기록 생략")
            return []

        written: list[str] = []
        remaining = dict(fills)

        # 1) 블록 내 기존 키 치환
        key_re = {
            s: re.compile(rf"^(?P<indent>\s+){re.escape(s)}\s*:\s*(?P<val>[^#]*)(?P<comment>#.*)?$") for s in fills
        }
        for idx in range(blk_start, blk_end):
            for stem in list(remaining):
                m = key_re[stem].match(lines[idx])
                if m:
                    lines[idx] = f"{m.group('indent')}{stem}: {_yaml_quote(remaining[stem])}"
                    written.append(stem)
                    del remaining[stem]
                    break

        # 2) 누락 키는 블록 끝에 삽입 (2-space 들여쓰기)
        if remaining:
            insert_at = blk_end
            # 블록 끝 공백 줄 앞에 삽입
            while insert_at - 1 >= blk_start and not lines[insert_at - 1].strip():
                insert_at -= 1
            new_lines = [f"  {stem}: {_yaml_quote(text)}" for stem, text in remaining.items()]
            lines[insert_at:insert_at] = new_lines
            written.extend(remaining)

        # 원자적 쓰기: tmp → rename (OOM kill 시 파일 유실 방지)
        import os as _os

        tmp = map_path.with_suffix(".yaml.tmp")
        tmp.write_text(nl.join(lines), encoding="utf-8")
        _os.replace(tmp, map_path)
    return written


# ─────────────────────────────────────────────────────────────────────────────
# 공개 진입점
# ─────────────────────────────────────────────────────────────────────────────
def fill_missing_ref_texts(
    base_voices_dir: Optional[Path] = None,
    map_path: Optional[Path] = None,
    force: bool = False,
) -> dict:
    """빈칸/누락 ref_texts 를 whisper 전사로 채움.

    반환: {"filled": {stem: text}, "skipped_no_target": bool, "failed": [{stem, error}]}
    """
    base_voices_dir = base_voices_dir or BASE_VOICES_DIR
    map_path = map_path or MAP_PATH

    targets = find_fillable(base_voices_dir, map_path, force=force)
    if not targets:
        logger.info("[TTS][transcribe] 채울 빈칸 없음 — whisper 미로드")
        return {"filled": {}, "skipped_no_target": True, "failed": []}

    logger.info(f"[TTS][transcribe] 전사 대상 {len(targets)}개: {targets}")
    fills: dict[str, str] = {}
    failed: list[dict] = []
    for stem in targets:
        wav = base_voices_dir / f"{stem}.wav"
        try:
            text = transcribe_one(wav)
            fills[stem] = text
            logger.info(f"[TTS][transcribe] {stem}: {text[:40]}{'…' if len(text) > 40 else ''}")
        except Exception as e:
            failed.append({"stem": stem, "error": str(e)})
            logger.warning(f"[TTS][transcribe] {stem} 전사 실패: {e}")

    written = apply_fills(map_path, fills)
    return {
        "filled": {s: fills[s] for s in written},
        "skipped_no_target": False,
        "failed": failed,
    }
