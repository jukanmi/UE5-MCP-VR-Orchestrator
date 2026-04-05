import os
import re
import json
from typing import Optional
from dotenv import load_dotenv
from langchain_openai import ChatOpenAI
from langchain_ollama import ChatOllama

load_dotenv()

# ==============================================================================
# 사용 가능한 모델 정의 (ollama pull <model_id> 로 사전 다운로드 필요)
# ==============================================================================
MODELS = {
    # Ollama 로컬 모델
    "qwen":     "huihui_ai/qwen3-vl-abliterated:8b-instruct",
    "qwen_slm": "qwen3:1.7b",    # 경량 보조 구조화 모델 (alias)
    "llama":    "llama3.3:70b",  # 대형 추론 모델 (고품질 필요 시)
    # OpenAI (API Key 필요)
    "openai":   "gpt-4o-mini",
}

# 모델 선택의 기본값 (서버 시작 시 모든 추론에서 사용)
DEFAULT_MODEL = "qwen"

OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL", "http://localhost:11434")


# ==============================================================================
# 통합 LLM 팩토리 함수
# get_llm / get_dialogue_llm을 통합 → 단일 인터페이스로 사용
# ==============================================================================
def get_llm(model_name: str = None, temperature: float = 0.0):
    """
    model_name: "qwen" | "qwen_slm" | "llama" | "openai" | None (→ DEFAULT_MODEL)
    temperature: 창의성 수준 (0.0 = 결정적, 1.0 = 창의적)
    """
    if model_name is None:
        model_name = DEFAULT_MODEL

    model_name = model_name.lower()

    if model_name in ("qwen", "qwen_slm", "llama"):
        model_id = MODELS.get(model_name, MODELS["qwen"])
        print(f"[LLM Factory] Ollama 모델 사용: {model_id}")
        return ChatOllama(
            model=model_id,
            temperature=temperature,
            base_url=OLLAMA_BASE_URL,
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
            f"[LLM Factory] 알 수 없는 model_name: '{model_name}'. "
            f"선택 가능: {list(MODELS.keys())}"
        )


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
        print("[LLM Factory] Ollama 직접 호출 (qwen_slm 구조화 용도)...")
        llm = get_llm("qwen_slm", temperature=0.1)
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
            start_marks = [output.find('['), output.find('{')]
            end_marks = [output.rfind(']'), output.rfind('}')]
            
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
