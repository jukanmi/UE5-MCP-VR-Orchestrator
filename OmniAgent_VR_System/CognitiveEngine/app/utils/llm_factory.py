import os
import subprocess
import re
import json
from typing import Optional
from dotenv import load_dotenv
from langchain_openai import ChatOpenAI
from langchain_google_genai import ChatGoogleGenerativeAI
from langchain_ollama import ChatOllama

load_dotenv()

MODELS = {
    "qwen": "qwen3:30b",
    "llama": "llama3.3:70b",
    "gemma-4b": "gemma-3-4b-it",
    "gemma-12b": "gemma-3-12b-it",
    "gemma-27b": "gemma-3-27b-it",
    "gemma": "gemma-3-27b-it",
    "gemini": "gemini-2.5-flash",
    "gemini-3": "gemini-3-flash",
    "qwen_slm": "qwen2.5:1.5b",
    "openai": "gpt-5-nano",
}

DIALOGUE_MODELS = {
    "extra": "gemma-4b",
    "normal": "gemma-12b",
    "core": "gemma-27b",
}

DEFAULT_MODEL = "gemini"

OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL")

GEMINI_CLI_COMMAND = os.getenv("GEMINI_CLI_COMMAND", "gemini")

def get_llm(model_name: str = None, temperature: float = 0.0):
    if model_name is None:
        model_name = DEFAULT_MODEL
    
    model_name = model_name.lower()
    
    if model_name in ["qwen", "llama", "qwen_slm"]:
        model_id = MODELS.get(model_name, MODELS["qwen"])
        
        print(f"[LLM Factory] Using Ollama model: {model_id}")
        
        return ChatOllama(
            model=model_id,
            temperature=temperature,
            base_url=OLLAMA_BASE_URL,
        )
    
    elif model_name in ["gemma", "gemma-4b", "gemma-12b", "gemma-27b", "gemini", "gemini-3"]:
        model_id = MODELS.get(model_name, MODELS["gemini"])
        
        print(f"[LLM Factory] Using Google model: {model_id}")
        
        return ChatGoogleGenerativeAI(
            model=model_id,
            temperature=temperature,
            google_api_key=os.getenv("GOOGLE_API_KEY"),
            convert_system_message_to_human=True
        )
    
    elif model_name == "openai":
        model_id = MODELS["openai"]
        
        print(f"[LLM Factory] Using OpenAI model: {model_id}")
        
        return ChatOpenAI(
            model=model_id,
            temperature=temperature,
            api_key=os.getenv("OPENAI_API_KEY"),
            max_retries=2
        )
    
    else:
        raise ValueError(f"Unknown model_name: {model_name}. Available: {list(MODELS.keys())}")


def get_dialogue_llm(importance: str = "normal", temperature: float = 0.7):
    importance = importance.lower()
    model_key = DIALOGUE_MODELS.get(importance, "gemma-12b")
    
    print(f"[LLM Factory] Dialogue model for '{importance}' importance: {MODELS[model_key]}")
    
    return get_llm(model_name=model_key, temperature=temperature)


def call_gemini_cli(prompt_text: str, extract_json: bool = True) -> Optional[str]:
    try:
        cmd = [
            "powershell", "-ExecutionPolicy", "Bypass", "-Command",
            f"{GEMINI_CLI_COMMAND} '{prompt_text}'"
        ]
        
        print(f"[LLM Factory] Calling Gemini CLI: {GEMINI_CLI_COMMAND}")
        
        result = subprocess.run(
            cmd, 
            capture_output=True, 
            text=True, 
            encoding='utf-8',
            timeout=60
        )
        
        if result.returncode != 0:
            print(f"[LLM Factory] CLI Error (code {result.returncode}): {result.stderr}")
            return None
        
        output = result.stdout
        
        if extract_json:
            json_match = re.search(r'```json\s*(.*?)\s*```', output, re.DOTALL)
            if json_match:
                extracted = json_match.group(1).strip()
                print(f"[LLM Factory] CLI Success: Extracted JSON ({len(extracted)} chars)")
                return extracted
            else:
                print("[LLM Factory] CLI Warning: No JSON block found, returning raw output")
        
        return output.strip()
        
    except subprocess.TimeoutExpired:
        print("[LLM Factory] CLI Timeout: Command took longer than 60 seconds")
        return None
    except Exception as e:
        print(f"[LLM Factory] CLI Exception: {e}")
        return None
