import logging
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


logger = logging.getLogger(__name__)
load_dotenv()

# ollama_structured 반환 타입 제네릭 — 호출 측이 캐스팅·getattr 없이 필드 직접 접근.
T = TypeVar("T", bound=BaseModel)

# Ollama 호출 공용 httpx 클라이언트 — 커넥션 풀 재사용(매 호출 TCP 핸드셰이크 회피).
# lazy init: 첫 호출 이벤트루프에 바인딩(서버 단일 루프 가정). 프로세스 수명 = client 수명.
# 구조화 호출·location_decision·prewarm 이 전부 이 하나를 쓴다. 기본 timeout 20s, 호출별 post 인자로 덮어쓴다.
_ollama_client: Optional["httpx.AsyncClient"] = None


def get_ollama_client() -> "httpx.AsyncClient":
    global _ollama_client
    if _ollama_client is None or _ollama_client.is_closed:
        _ollama_client = httpx.AsyncClient(timeout=20.0)
    return _ollama_client


def _extract_json_from_thinking(thinking: str) -> str:
    """클라우드 thinking 모델이 content 를 비우고 thinking 안에 JSON 을 남긴 경우 추출.
    마지막 { ... } 블록을 탐색 — 모델이 "Thus JSON:" 뒤에 최종 답을 쓰는 패턴."""
    if not thinking:
        return ""
    # 마지막 { 위치부터 역방향으로 matching } 찾기
    last_open = thinking.rfind("{")
    if last_open == -1:
        return ""
    # last_open 이후 닫는 괄호 균형 맞추기
    depth = 0
    for i, ch in enumerate(thinking[last_open:], last_open):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return thinking[last_open : i + 1]
    return ""


def _strip_markdown_json(content: str) -> str:
    """클라우드 모델이 ```json ... ``` 로 감싸 반환하는 경우 내부 JSON만 추출."""
    stripped = content.strip()
    if stripped.startswith("```"):
        lines = stripped.splitlines()
        inner = lines[1:-1] if lines[-1].strip() == "```" else lines[1:]
        return "\n".join(inner).strip()
    return stripped


# 컨텍스트 창. 2048 이었을 때 Stage1 프롬프트(persona+RAG 3청크+기록 5턴+지시문)가 2011 토큰을 먹어
# 생성 여유 37 토큰 → JSON 이 문자열 중간에서 잘려 NPC 가 "..." 만 말했다(2026-09-18 Ollama 로그
# `n_ctx_slot = 2048, task.n_tokens = 2011, truncated = 1` 실측). KV 캐시 비용은 e4b 기준 수백 MB.
NUM_CTX = 4096

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
    # v3: 3806행 재학습(v2 대비 5배 데이터), gold 재현 6/8→8/8, held-out 0/6→2/6.
    #
    # 2026-09-05 v1 으로 되돌림 — 아이템 전달 회귀 때문.
    # 동일 조건(Moca, 인벤 7종, 대화기록 비움, 질문 3종×2회) 실측:
    #   v1  "돌 좀 줘"/"붕대 건네줘" → GiveItem  (플레이어 인벤토리에 실제 추가)
    #   v2  동일 질문             → HandObject (NPC 가 들고만 있음, 인벤 무변화)
    #   v3  동일 질문             → HandObject (v2 와 동일)
    # v2 에서 생긴 회귀이며 프롬프트가 아니라 학습 데이터에서 갈린다. HandObject 는
    # EquipItem 만 하므로 "줘" 요청이 게임상 아무 효과 없이 끝난다.
    # ItemID 정확도는 세 버전 모두 4/4 로 동일. 대가: v1 은 대사가 다소 장황하다.
    # 롤백: "gemma4-e4b-dialogue-v3" 로 원복(대사 품질 우선 시).
    "gemma4_slm": "gemma4-e4b-dialogue-v1",
    "gemma4_31b": "gemma4:31b",  # 최고 품질 (고부하 작업 시)
    "gemma4_e2b": "gemma4:e2b",  # 초경량 (지연 민감 구간)
    # 폴백 후보 (경량, 로컬 pull 됨)
    "qwen_slm": "qwen3:1.7b",
    # ---------------------------------------------------------------------------
    # Ollama 클라우드 모델 — ollama.com 계정 로그인 필요 (Ollama 앱에서 인증)
    # 사용: get_llm("cloud_deepseek_flash") / ollama_structured(model_name="cloud_glm") 등
    # 주의: 클라우드 모델은 format=schema grammar 강제 미지원 — JSON을 마크다운 블록으로
    #       감싸 반환. ollama_structured 가 자동 strip 처리하므로 호출 측 변경 불필요.
    # 2026-08-15 기준 활성 모델 (retired: deepseek-v3.1:671b, qwen3-coder:480b 제거)
    # ---------------------------------------------------------------------------
    "cloud_deepseek_flash": "deepseek-v4-flash:cloud",  # 빠른 응답, 284B MoE
    "cloud_deepseek_pro": "deepseek-v4-pro",  # 전술 추론 깊이 (pull 필요)
    "cloud_glm": "glm-5.2",  # 한국어 우수, 장기 작업 강점 (pull 필요)
    "cloud_kimi": "kimi-k3",  # 멀티모달+추론 (pull 필요)
    "cloud_gpt_large": "gpt-oss:120b-cloud",  # 설치됨, 동작 확인
    "cloud_gpt_small": "gpt-oss:20b-cloud",  # 미테스트
    "cloud_gemma4": "gemma4:cloud",  # 31B, 무료 티어 가능(2026-09-17 실측). 스토리 디렉터 전용
    # Stage2 전용 31B. 로드 없음·VRAM 0, 같은 계획 프롬프트 0.77~1.24s(2026-10-08 실측,
    # 로컬 qwen3:8b 는 keep_alive 만료 후 첫 호출 2.4s+). 스토리 디렉터 키와 분리해 서로 영향 없게 한다.
    "cloud_gemma4_31b": "gemma4:31b-cloud",
}

# 모델 선택의 기본값 (서버 시작 시 모든 추론에서 사용)
# Stage2 플래너·get_llm() 폴백 — 여기 한 줄만 바꾸면 Stage2+get_llm 전체 반영.
# 2026-09-07 12B(7.4GB) → 8B(5.2GB). 플래너는 replan 때만 도는데 12B 는 로드가 느리고
# VRAM 을 크게 물어 SDXL·PIE 와 부딪혔다. 되돌리려면 "gemma4" 로.
# 이 값은 이제 Stage2 의 **로컬 폴백**(클라우드 실패 시에만 로드)이자 get_llm() 기본 모델이다.
STAGE2_MODEL = "mid"
# Stage2 기본 경로 = 클라우드(로컬 VRAM 확보 + 재로드 지연 제거). 끄려면 .env/환경변수 STAGE2_USE_CLOUD=0
# → 로컬 STAGE2_MODEL 만 사용(오프라인·무료 티어 소진 시). 기본 켜짐.
STAGE2_CLOUD_ENABLED = os.getenv("STAGE2_USE_CLOUD", "1").strip().lower() not in ("0", "false", "off", "no")
STAGE2_CLOUD_MODEL = "cloud_gemma4_31b"
# 클라우드 호출 상한(초) — 넘기면 로컬 폴백. 실측 정상 응답 0.8~1.3s 라 4s 면 일시 지연은 흡수하고 장애는 빨리 끊는다.
# ollama_structured 의 httpx timeout 과 wait_for 에 같은 값을 쓴다(상한이 이 상수 하나).
STAGE2_CLOUD_TIMEOUT_S = 4.0
# 연속 실패 N회면 COOLDOWN 초 동안 클라우드를 건너뛰고 바로 로컬로 — 장애 중 매 replan 이 타임아웃을 또 내지 않게.
# 성공 1회로 카운터 리셋. 모듈 전역(서버 단일 프로세스·단일 루프 가정).
STAGE2_CLOUD_FAIL_LIMIT = 2
STAGE2_CLOUD_COOLDOWN_S = 90.0
_stage2_cloud_fails = 0
_stage2_cloud_skip_until = 0.0  # time.monotonic() 기준, 이 시각 전까지 클라우드 생략
# Stage1 대화·액션 결정 (hot loop) — 파인튜닝 SLM. 교체 시 여기만.
STAGE1_MODEL = "gemma4_slm"
# 스토리 디렉터(app/story) — 비트 전이 시만 호출. 31B 클라우드, 무료 티어 동시 1.
STORY_MODEL = "cloud_gemma4"

OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL", "http://localhost:11434").rstrip("/")

# 페르소나 importance → MODELS 키. 디버그 대시보드 배지·API 응답이 여기서 읽는다 — 모델을 바꿀 때
# main.py·debug.html 을 따로 고치지 않게 하려고 한 곳에 둔다.
IMPORTANCE_MODELS = {"core": "gemma4", "high": "mid", "normal": STAGE1_MODEL}

# Stage1 SLM(e4b) 상주 시간. 5m 이면 유휴 5분 뒤 첫 턴이 콜드 재로드를 맞는다 → 30m 로 늘리고,
# main.py 가 WS 연결 중 SLM_PING_INTERVAL_S 마다 로드콜로 타이머를 갱신해 세션 내내 상주시킨다.
SLM_KEEP_ALIVE = "30m"
SLM_PING_INTERVAL_S = 240.0  # 4분 — 기본 5m 타이머보다 짧아 핑 1회 누락에도 안 내려간다


def _keep_alive_for(model_name: str) -> str:
    """플래너 로컬 폴백(Stage2 'mid')은 폴백 때만 쓰는 큰 모델 → idle squat 방지로 30s (replan 버스트의
    Stage2+supervisor 연속 호출은 30s 윈도로 브릿지, 이후 자동 언로드). Stage1 SLM 은 매 턴 쓰는 hot-loop
    라 SLM_KEEP_ALIVE(30m, 콜드 재로드 회피). 그 외(12B core·e2b 요약 등)는 5m — 큰 모델을 길게 잡지 않는다.
    main.py 의 prewarm keep_alive 도 이 값과 맞춰야 squat 정책이 덮어써지지 않는다."""
    if model_name == STAGE2_MODEL:
        return "30s"
    return SLM_KEEP_ALIVE if model_name == STAGE1_MODEL else "5m"


def model_for_importance(importance: str) -> str:
    """importance("normal"|"high"|"core") → 실제 Ollama 모델 ID. 모르는 값은 normal 취급."""
    return MODELS[IMPORTANCE_MODELS.get(importance, IMPORTANCE_MODELS["normal"])]


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

    if model_name in MODELS:
        model_id = MODELS[model_name]
        logger.info(f"[LLM Factory] Ollama 모델 사용: {model_id}")
        keep_alive = _keep_alive_for(model_name)
        return ChatOllama(
            model=model_id,
            temperature=temperature,
            base_url=OLLAMA_BASE_URL,
            num_ctx=NUM_CTX,
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
    num_ctx: int = NUM_CTX,
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
        # 플래너(Stage2)는 idle squat 방지 30s, 경량 hot 모델은 5m (get_llm 과 정합).
        "keep_alive": _keep_alive_for(model_name),
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
    client = get_ollama_client()
    started = time.perf_counter()
    content = ""
    try:
        resp = await client.post(f"{OLLAMA_BASE_URL}/api/chat", json=body, timeout=timeout)
        resp.raise_for_status()
        # 응답 구조 변경·에러 시 KeyError 대신 명시적 예외 — content 없으면 호출처 폴백 가능.
        data = resp.json()
        msg = data.get("message") or {}
        content = msg.get("content") or ""
        # 잘림 진단 — length = num_predict 소진 또는 num_ctx 잔여 소진. 조용히 "..." 로 떨어지는 걸 막는 단서.
        if data.get("done_reason") == "length":
            logger.warning(
                f"[LLM] {model_id} 출력 잘림(done_reason=length): prompt={data.get('prompt_eval_count')} "
                f"gen={data.get('eval_count')} num_ctx={num_ctx} num_predict={num_predict}"
            )
        if not content:
            # 클라우드 thinking 모델: think=False 무시 → thinking 토큰이 num_predict 소비 후
            # content 미출력. thinking 필드 마지막 JSON 블록 추출로 폴백.
            content = _extract_json_from_thinking(msg.get("thinking") or "")
        if not content:
            raise ValueError(f"Ollama 구조화 응답에 content 없음: {data}")
        # 클라우드 모델은 format=schema grammar 미강제 → ```json ... ``` 마크다운 래핑.
        # 로컬 모델은 이미 순수 JSON이므로 strip 무해.
        content = _strip_markdown_json(content)
        parsed_obj = schema_model.model_validate_json(content)
    except Exception as e:
        _log(content, None, str(e), (time.perf_counter() - started) * 1000)
        raise
    _log(content, parsed_obj.model_dump(), "", (time.perf_counter() - started) * 1000)
    return parsed_obj


async def stage2_structured(
    system: str,
    user: str,
    schema_model: Type[T],
    **kwargs,
) -> T:
    """Stage2 구조화 호출 — 클라우드 우선, 실패 시 로컬 STAGE2_MODEL 로 같은 프롬프트 1회 폴백.
    폴백 조건: 예외·타임아웃(STAGE2_CLOUD_TIMEOUT_S)·JSON/스키마 파싱 실패(ollama_structured 가 raise 하는 전부).
    클라우드는 grammar 미지원이라 ollama_structured 안의 마크다운 펜스·thinking JSON 추출기를 그대로 탄다.
    연속 STAGE2_CLOUD_FAIL_LIMIT 회 실패 시 STAGE2_CLOUD_COOLDOWN_S 동안 클라우드를 생략한다.
    로컬 폴백 모델은 폴백 때만 로드된다(평소 VRAM 상주 없음). kwargs 는 ollama_structured 로 전달."""
    global _stage2_cloud_fails, _stage2_cloud_skip_until
    fell_back = False
    if STAGE2_CLOUD_ENABLED:
        now = time.monotonic()
        if now < _stage2_cloud_skip_until:
            fell_back = True  # 쿨다운 중 — 클라우드 시도 없이 바로 로컬
        else:
            if _stage2_cloud_skip_until:  # 쿨다운이 끝난 첫 시도
                _stage2_cloud_skip_until = 0.0
                logger.info("[Stage2] 클라우드 쿨다운 해제 — 재시도")
            try:
                result = await asyncio.wait_for(
                    ollama_structured(
                        system,
                        user,
                        schema_model,
                        **{**kwargs, "model_name": STAGE2_CLOUD_MODEL, "timeout": STAGE2_CLOUD_TIMEOUT_S},
                    ),
                    timeout=STAGE2_CLOUD_TIMEOUT_S,
                )
                _stage2_cloud_fails = 0
                return result
            except Exception as e:
                fell_back = True
                _stage2_cloud_fails += 1
                logger.warning(
                    f"[Stage2] 클라우드({MODELS[STAGE2_CLOUD_MODEL]}) 실패 → 로컬 {MODELS[STAGE2_MODEL]} 폴백: "
                    f"{type(e).__name__}: {e}"
                )
                if _stage2_cloud_fails >= STAGE2_CLOUD_FAIL_LIMIT:
                    _stage2_cloud_fails = 0
                    _stage2_cloud_skip_until = time.monotonic() + STAGE2_CLOUD_COOLDOWN_S
                    logger.warning(
                        f"[Stage2] 클라우드 연속 {STAGE2_CLOUD_FAIL_LIMIT}회 실패 — "
                        f"{STAGE2_CLOUD_COOLDOWN_S:.0f}s 동안 로컬만 사용"
                    )
    # 로컬 호출: 클라우드용 timeout 이 새지 않게 kwargs 의 timeout 은 호출자가 준 것만 유지한다.
    local_kwargs = {**kwargs, "model_name": STAGE2_MODEL}
    if fell_back and kwargs.get("log_extra") is not None:
        local_kwargs["log_extra"] = {**kwargs["log_extra"], "fallback_from_cloud": True}
    return await ollama_structured(system, user, schema_model, **local_kwargs)


# NOTE: 과거 call_ollama_direct(자유텍스트 단발 생성)는 유일 호출처였던 dialogue.py
# Stage1 폴백이 구조화 재시도(ollama_structured)로 전환되며 고아화되어 제거.
# 자유텍스트 단발이 다시 필요하면 ollama_structured(grammar 강제) 사용이 정답.
