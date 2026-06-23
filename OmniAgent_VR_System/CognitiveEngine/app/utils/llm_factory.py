import os
import re
from typing import Optional, Type
from dotenv import load_dotenv
from langchain_openai import ChatOpenAI
from langchain_ollama import ChatOllama
import httpx
from pydantic import BaseModel

load_dotenv()

# ==============================================================================
# 사용 가능한 모델 정의 (ollama pull <model_id> 로 사전 다운로드 필요)
# ==============================================================================
MODELS = {
    # Ollama 로컬 모델 — Gemma 4
    # 메인 LLM (대화/추론) — 26b(17GB)는 VRAM 16GB 미적합(CPU 오프로드)이라 12B Q4 로 교체.
    # gemma4-12b 는 hf.co/mradermacher/Gemma-4-12B-OBLITERATED-GGUF:Q4_K_M 의 ollama cp 별칭.
    "gemma4": "gemma4-12b",
    "mid": "qwen3:8b",  # 중간 품질 (high NPC용 — e4b보다 낫고 core 12B보다 빠름)
    "gemma4_slm": "gemma4:e4b",  # 경량 구조화 모델 (JSON 추출 등)
    "gemma4_31b": "gemma4:31b",  # 최고 품질 (고부하 작업 시)
    "gemma4_e2b": "gemma4:e2b",  # 초경량 (지연 민감 구간)
    # 기존 모델 (폴백 용도)
    "qwen": "huihui_ai/qwen3-vl-abliterated:8b-instruct",
    "qwen_slm": "qwen3:1.7b",
    "llama": "llama3.3:70b",
    # OpenAI (API Key 필요)
    "openai": "gpt-4o-mini",
}

# 모델 선택의 기본값 (서버 시작 시 모든 추론에서 사용)
DEFAULT_MODEL = "gemma4"

OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL", "http://localhost:11434")


# ==============================================================================
# 통합 LLM 팩토리 함수
# get_llm / get_dialogue_llm을 통합 → 단일 인터페이스로 사용
# ==============================================================================
def get_llm(model_name: str = None, temperature: float = 0.0, num_predict: int = 150):
    """
    model_name:  "qwen" | "qwen_slm" | "llama" | "openai" | None (→ DEFAULT_MODEL)
    temperature: 창의성 수준 (0.0 = 결정적, 1.0 = 창의적)
    num_predict: 최대 출력 토큰 수 (대화용은 300, 구조화/요약용은 150)
    """
    if model_name is None:
        model_name = DEFAULT_MODEL

    model_name = model_name.lower()

    OLLAMA_MODELS = {"gemma4", "mid", "gemma4_slm", "gemma4_31b", "gemma4_e2b", "qwen", "qwen_slm", "llama"}
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

    elif model_name == "openai":
        model_id = MODELS["openai"]
        print(f"[LLM Factory] OpenAI 모델 사용: {model_id}")
        return ChatOpenAI(
            model=model_id,
            temperature=temperature,
            api_key=os.getenv("OPENAI_API_KEY"),
            max_retries=2,
        )

    else:
        raise ValueError(
            f"[LLM Factory] 알 수 없는 model_name: '{model_name}'. 선택 가능: {list(MODELS.keys()) + ['openai']}"
        )


# ==============================================================================
# Ollama 직접 구조화 호출 (format=schema) — langchain with_structured_output 우회
# ==============================================================================
# WHY: langchain with_structured_output 은 e4b 구조화에서 warm avg ~3500ms(분산 1.2~8s)
#      오버헤드 발생(실측). 동일 JSON 스키마를 Ollama /api/chat 의 format 으로 직접 주면
#      ~1200ms(안정) — 2.9배. grammar 강제(필드 required)는 동일하게 보장.
async def ollama_structured(
    system: str,
    user: str,
    schema_model: Type[BaseModel],
    *,
    model_name: str = "gemma4_slm",
    temperature: float = 0.7,
    num_predict: int = 300,
    num_ctx: int = 2048,
    timeout: float = 60.0,
) -> BaseModel:
    """Ollama /api/chat 직접 호출 → schema_model 인스턴스 반환.
    format 에 model_json_schema() 를 전달해 토큰 grammar 로 필드 생성을 강제."""
    model_id = MODELS.get(model_name, MODELS["gemma4"])
    body = {
        "model": model_id,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
        "stream": False,
        "format": schema_model.model_json_schema(),
        "think": False,  # reasoning 토큰이 num_predict 잠식 방지 (get_llm reasoning=False 와 정합)
        # 12B core 는 idle squat 방지 30s, 경량 hot 모델은 5m (get_llm 과 정합).
        "keep_alive": "30s" if model_name == "gemma4" else "5m",
        "options": {"temperature": temperature, "num_ctx": num_ctx, "num_predict": num_predict},
    }
    async with httpx.AsyncClient(timeout=timeout) as client:
        resp = await client.post(f"{OLLAMA_BASE_URL}/api/chat", json=body)
        resp.raise_for_status()
        content = resp.json()["message"]["content"]
    return schema_model.model_validate_json(content)


# ==============================================================================
# Ollama 직접 호출 유틸리티 (JSON 구조화 등 단발성 추론에 사용)
# ==============================================================================
def call_ollama_direct(prompt_text: str, extract_json: bool = True) -> Optional[str]:
    """
    경량 SLM(qwen_slm)을 사용해 단발성 텍스트/JSON 추론을 즉시 수행합니다.
    - extract_json=True : 응답에서 JSON 블록을 자동으로 파싱/추출
    - extract_json=False: 응답 전체 텍스트를 그대로 반환
    """
    try:
        print("[LLM Factory] Ollama 직접 호출 (gemma4_slm 구조화 용도)...")
        llm = get_llm("gemma4_slm", temperature=0.1)
        response = llm.invoke(prompt_text)

        output = response.content if hasattr(response, "content") else str(response)

        if extract_json:
            # ```json ... ``` 블록 우선 파싱
            json_match = re.search(r"```json\s*(.*?)\s*```", output, re.DOTALL)
            if json_match:
                extracted = json_match.group(1).strip()
                print(f"[LLM Factory] JSON 추출 성공 ({len(extracted)} chars)")
                return extracted

            # 블록 없이 JSON 기호([ 또는 {)가 있는 경우 폴백
            start_marks = [output.find("["), output.find("{")]
            end_marks = [output.rfind("]"), output.rfind("}")]

            valid_starts = [i for i in start_marks if i != -1]
            valid_ends = [i for i in end_marks if i != -1]

            if valid_starts and valid_ends:
                idx_start = min(valid_starts)
                idx_end = max(valid_ends) + 1
                return output[idx_start:idx_end].strip()

            print("[LLM Factory] 경고: JSON 블록 없음 → 원문 반환")

        return output.strip()

    except Exception as e:
        print(f"[LLM Factory] 오류: {e}")
        return None
