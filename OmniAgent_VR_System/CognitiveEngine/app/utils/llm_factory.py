import asyncio
import os
import time
from typing import Optional, Type, TypeVar
from dotenv import load_dotenv
from langchain_ollama import ChatOllama
import httpx
from pydantic import BaseModel

from .async_tasks import spawn_background
from .train_logger import log_llm_call

load_dotenv()

# ollama_structured 반환 타입 제네릭 — 호출 측이 캐스팅·getattr 없이 필드 직접 접근.
T = TypeVar("T", bound=BaseModel)

# ollama_structured 전용 전역 httpx 클라이언트 — 커넥션 풀 재사용(매 호출 TCP 핸드셰이크 회피).
# lazy init: 첫 호출 이벤트루프에 바인딩(서버 단일 루프 가정). 프로세스 수명 = client 수명.
_structured_client: Optional["httpx.AsyncClient"] = None


def _get_structured_client() -> "httpx.AsyncClient":
    global _structured_client
    if _structured_client is None:
        _structured_client = httpx.AsyncClient()
    return _structured_client


# ==============================================================================
# 사용 가능한 모델 정의 (ollama pull <model_id> 로 사전 다운로드 필요)
# ==============================================================================
MODELS = {
    # Ollama 로컬 모델 — Gemma 4
    # 메인 LLM (대화/추론) — 26b(17GB)는 VRAM 16GB 미적합(CPU 오프로드)이라 12B Q4 로 교체.
    # gemma4-12b 는 hf.co/mradermacher/Gemma-4-12B-OBLITERATED-GGUF:Q4_K_M 의 ollama cp 별칭.
    "gemma4": "gemma4-12b",
    "mid": "qwen3:8b",  # 중간 품질 (high NPC용 — e4b보다 낫고 core 12B보다 빠름)
    # 경량 구조화 모델 (JSON 추출 등) — LoRA 파인튜닝판(SPEC_finetune M2, 2026-07-21).
    # 액션선택 정확도 개선(gold 재현 3/8→8/8, held-out 의도인식 0/6→2/6) — finetune/RESULT.md.
    # 롤백: "gemma4:e4b" 로 원복(기존 태그 불변).
    "gemma4_slm": "gemma4-e4b-dialogue-v2",
    "gemma4_31b": "gemma4:31b",  # 최고 품질 (고부하 작업 시)
    "gemma4_e2b": "gemma4:e2b",  # 초경량 (지연 민감 구간)
    # 폴백 후보 (경량, 로컬 pull 됨)
    "qwen_slm": "qwen3:1.7b",
    # ---------------------------------------------------------------------------
    # Ollama 클라우드 모델 — OLLAMA_API_KEY 환경변수 필요 (ollama.com에서 발급)
    # 사용: get_llm("cloud_qwen") / get_llm("cloud_deepseek") 등
    # ---------------------------------------------------------------------------
    "cloud_qwen": "qwen3-coder:480b-cloud",  # JSON 구조화 최강, 한국어 우수
    "cloud_deepseek": "deepseek-v3.1:671b-cloud",  # 전술 추론 깊이 우수
    "cloud_deepseek_flash": "deepseek-v4-flash",  # 빠른 응답 위주
    "cloud_gpt_large": "gpt-oss:120b-cloud",  # GPT 계열 대형
    "cloud_gpt_small": "gpt-oss:20b-cloud",  # GPT 계열 경량
}

# 모델 선택의 기본값 (서버 시작 시 모든 추론에서 사용)
# Stage2 플래너·get_llm() 폴백 — 여기 한 줄만 바꾸면 Stage2+get_llm 전체 반영.
STAGE2_MODEL = "gemma4"
# Stage1 대화·액션 결정 (hot loop) — 파인튜닝 SLM. 교체 시 여기만.
STAGE1_MODEL = "gemma4_slm"

OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL", "http://localhost:11434")


# ==============================================================================
# 통합 LLM 팩토리 함수
# get_llm / get_dialogue_llm을 통합 → 단일 인터페이스로 사용
# ==============================================================================
def get_llm(model_name: str = None, temperature: float = 0.0, num_predict: int = 150):
    """
    model_name:  "gemma4" | "gemma4_slm" | "gemma4_e2b" | "qwen_slm" | None (→ STAGE2_MODEL)
    temperature: 창의성 수준 (0.0 = 결정적, 1.0 = 창의적)
    num_predict: 최대 출력 토큰 수 (대화용은 300, 구조화/요약용은 150)
    """
    if model_name is None:
        model_name = STAGE2_MODEL

    model_name = model_name.lower()

    OLLAMA_MODELS = {
        "gemma4",
        "mid",
        "gemma4_slm",
        "gemma4_31b",
        "gemma4_e2b",
        "qwen_slm",
        # 클라우드 모델 — 동일 Ollama 엔드포인트, OLLAMA_API_KEY 인증 추가됨
        "cloud_qwen",
        "cloud_deepseek",
        "cloud_deepseek_flash",
        "cloud_gpt_large",
        "cloud_gpt_small",
    }
    if model_name in OLLAMA_MODELS:
        model_id = MODELS.get(model_name, MODELS["gemma4"])
        print(f"[LLM Factory] Ollama 모델 사용: {model_id}")
        # keep_alive: 12B core(gemma4)는 replan 때만 쓰는 8GB 모델 → idle squat 방지로 30s 단축
        # (replan 버스트 Stage2+supervisor 연속 호출은 30s 윈도로 브릿지, 이후 자동 언로드).
        # e4b 등 hot-loop 경량 모델은 5m 유지(매 턴 사용, 콜드 재로드 회피).
        keep_alive = "30s" if model_name == "gemma4" else "5m"
        return ChatOllama(
            model=model_id,
            temperature=temperature,
            base_url=OLLAMA_BASE_URL,
            num_ctx=2048,
            num_predict=num_predict,
            num_thread=8,
            request_timeout=30.0,
            keep_alive=keep_alive,
            # think=false — gemma4/qwen3 계열은 thinking 모델이라 사고 토큰이
            # num_predict 예산을 잠식해 content="" 로 잘림 (12B 실측: 200토큰 전부
            # thinking, content 빈 문자열). 대화는 즉답만 필요.
            reasoning=False,
        )

    else:
        raise ValueError(f"[LLM Factory] 알 수 없는 model_name: '{model_name}'. 선택 가능: {list(MODELS.keys())}")


# ==============================================================================
# Ollama 직접 구조화 호출 (format=schema) — langchain with_structured_output 우회
# ==============================================================================
# WHY: langchain with_structured_output 은 e4b 구조화에서 warm avg ~3500ms(분산 1.2~8s)
#      오버헤드 발생(실측). 동일 JSON 스키마를 Ollama /api/chat 의 format 으로 직접 주면
#      ~1200ms(안정) — 2.9배. grammar 강제(필드 required)는 동일하게 보장.
async def ollama_structured(
    system: str,
    user: str,
    schema_model: Type[T],
    *,
    model_name: str = "gemma4_slm",
    temperature: float = 0.7,
    num_predict: int = 300,
    num_ctx: int = 2048,
    timeout: float = 60.0,
    schema_override: Optional[dict] = None,
    log_extra: Optional[dict] = None,
) -> T:
    """Ollama /api/chat 직접 호출 → schema_model 인스턴스 반환.
    format 에 model_json_schema() 를 전달해 토큰 grammar 로 필드 생성을 강제.
    schema_override: 호출별 동적 제약(예: target enum 주입) 시 가공된 스키마 dict 전달 —
    grammar 만 좁히고 검증은 여전히 schema_model(필드 str)로 수행.
    log_extra: 지정 시 파인튜닝 로그(train_logger) 기록 — {"stage","msg_id","npc_id",...}.
    성공·실패 양쪽 기록, fire-and-forget 라 핫패스 지연 없음."""
    model_id = MODELS.get(model_name, MODELS["gemma4"])
    body = {
        "model": model_id,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
        "stream": False,
        "format": schema_override if schema_override is not None else schema_model.model_json_schema(),
        "think": False,  # reasoning 토큰이 num_predict 잠식 방지 (get_llm reasoning=False 와 정합)
        # 12B core 는 idle squat 방지 30s, 경량 hot 모델은 5m (get_llm 과 정합).
        "keep_alive": "30s" if model_name == "gemma4" else "5m",
        "options": {"temperature": temperature, "num_ctx": num_ctx, "num_predict": num_predict},
    }

    def _log(raw: str, parsed: Optional[dict], error: str, elapsed_ms: float) -> None:
        if log_extra is None:
            return
        spawn_background(
            asyncio.to_thread(
                log_llm_call,
                stage=log_extra.get("stage", "unknown"),
                model_id=model_id,
                system_prompt=system,
                user_prompt=user,
                raw_response=raw,
                parsed=parsed,
                error=error,
                elapsed_ms=elapsed_ms,
                temperature=temperature,
                extra={k: v for k, v in log_extra.items() if k != "stage"},
            ),
            label="train-log",
        )

    # 매 호출 새 AsyncClient 생성 = TCP 핸드셰이크 오버헤드(멀티 NPC 동시 시 가중).
    # 모듈 전역 client 재사용으로 커넥션 풀 유지. timeout 은 호출별 post 인자로 전달.
    client = _get_structured_client()
    started = time.perf_counter()
    content = ""
    try:
        resp = await client.post(f"{OLLAMA_BASE_URL}/api/chat", json=body, timeout=timeout)
        resp.raise_for_status()
        # 응답 구조 변경·에러 시 KeyError 대신 명시적 예외 — content 없으면 호출처 폴백 가능.
        content = (resp.json().get("message") or {}).get("content") or ""
        if not content:
            raise ValueError(f"Ollama 구조화 응답에 content 없음: {resp.json()}")
        parsed_obj = schema_model.model_validate_json(content)
    except Exception as e:
        _log(content, None, str(e), (time.perf_counter() - started) * 1000)
        raise
    _log(content, parsed_obj.model_dump(), "", (time.perf_counter() - started) * 1000)
    return parsed_obj


# NOTE: 과거 call_ollama_direct(자유텍스트 단발 생성)는 유일 호출처였던 dialogue.py
# Stage1 폴백이 구조화 재시도(ollama_structured)로 전환되며 고아화되어 제거.
# 자유텍스트 단발이 다시 필요하면 ollama_structured(grammar 강제) 사용이 정답.
