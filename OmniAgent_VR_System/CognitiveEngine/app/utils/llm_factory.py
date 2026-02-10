"""
File: llm_factory.py
Purpose: Centralized LLM Provider.
Supports Ollama (local), Gemini, Gemma, and OpenAI models.
Includes NPC importance-based model selection for dialogue.
Also provides Gemini CLI wrapper for quota-free operations.
"""
import os
import subprocess
import re
import json
from typing import Optional
from dotenv import load_dotenv
from langchain_openai import ChatOpenAI
from langchain_google_genai import ChatGoogleGenerativeAI
from langchain_ollama import ChatOllama

# Load .env from root of CognitiveEngine
load_dotenv()

# Available models
MODELS = {
    # Ollama (local)
    "qwen": "qwen3:30b",                 # Qwen 3 30B (local)
    "llama": "llama3.3:70b",             # Llama 3.3 70B (local)
    # Google AI Studio - Gemma 3 (for dialogue by importance)
    "gemma-4b": "gemma-3-4b-it",         # Gemma 3 4B - Extra NPCs
    "gemma-12b": "gemma-3-12b-it",       # Gemma 3 12B - Normal NPCs
    "gemma-27b": "gemma-3-27b-it",       # Gemma 3 27B - Core NPCs
    "gemma": "gemma-3-27b-it",           # Alias for 27B
    # Google AI Studio - Gemini (for structured output)
    "gemini": "gemini-2.5-flash",        # Gemini 2.5 Flash (fast)
    "gemini-3": "gemini-3-flash",        # Gemini 3 Flash (fast)
    # OpenAI
    "openai": "gpt-5-nano",              # GPT-5 Nano
}

# NPC Importance to Model mapping
DIALOGUE_MODELS = {
    "extra": "gemma-4b",     # 엑스트라 NPC
    "normal": "gemma-12b",   # 일반 중요 인물
    "core": "gemma-27b",     # 핵심 인물
}

# Default model - change this to switch globally
DEFAULT_MODEL = "gemini"  # Options: "qwen", "llama", "gemma", "gemini", "gemini-3", "openai"

# Ollama server URL (default: localhost)
OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL")

# Gemini CLI command (default: "gemini")
# Set this in .env if you need a custom path, e.g., GEMINI_CLI_COMMAND="C:\path\to\gemini.exe"
GEMINI_CLI_COMMAND = os.getenv("GEMINI_CLI_COMMAND", "gemini")

def get_llm(model_name: str = None, temperature: float = 0.0):
    """
    Returns a configured LLM instance.
    
    Args:
        model_name: Model to use. Options: "qwen", "llama", "gemma", "gemma-4b", "gemma-12b", "gemma-27b", "gemini", "gemini-3", "openai"
                    If None, uses DEFAULT_MODEL.
        temperature: Sampling temperature (0.0 = deterministic, 1.0 = creative)
    
    Returns:
        Configured LangChain chat model instance.
    """
    if model_name is None:
        model_name = DEFAULT_MODEL
    
    model_name = model_name.lower()
    
    # Ollama models (local)
    if model_name in ["qwen", "llama"]:
        model_id = MODELS.get(model_name, MODELS["qwen"])
        
        print(f"[LLM Factory] Using Ollama model: {model_id}")
        
        return ChatOllama(
            model=model_id,
            temperature=temperature,
            base_url=OLLAMA_BASE_URL,
        )
    
    # Google models (Gemma / Gemini)
    elif model_name in ["gemma", "gemma-4b", "gemma-12b", "gemma-27b", "gemini", "gemini-3"]:
        model_id = MODELS.get(model_name, MODELS["gemini"])
        
        print(f"[LLM Factory] Using Google model: {model_id}")
        
        return ChatGoogleGenerativeAI(
            model=model_id,
            temperature=temperature,
            google_api_key=os.getenv("GOOGLE_API_KEY"),
            convert_system_message_to_human=True
        )
    
    # OpenAI models
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
    """
    Returns an LLM instance for NPC dialogue based on importance.
    
    Args:
        importance: NPC importance level: "extra", "normal", or "core"
        temperature: Sampling temperature (default 0.7 for creative dialogue)
    
    Returns:
        Configured LangChain chat model for dialogue.
    
    Model mapping:
        - extra: Gemma 3 4B (엑스트라 NPC)
        - normal: Gemma 3 12B (일반 중요 인물)
        - core: Gemma 3 27B (핵심 인물)
    """
    importance = importance.lower()
    model_key = DIALOGUE_MODELS.get(importance, "gemma-12b")
    
    print(f"[LLM Factory] Dialogue model for '{importance}' importance: {MODELS[model_key]}")
    
    return get_llm(model_name=model_key, temperature=temperature)


def call_gemini_cli(prompt_text: str, extract_json: bool = True) -> Optional[str]:
    """
    Calls Gemini CLI via subprocess to bypass API quota limits.
    
    Args:
        prompt_text: The prompt to send to Gemini CLI
        extract_json: If True, attempts to extract JSON from markdown code blocks
    
    Returns:
        The CLI output (raw or extracted JSON string), or None if failed
    
    Usage:
        # Get raw output
        result = call_gemini_cli("Analyze this text")
        
        # Get extracted JSON
        json_str = call_gemini_cli("Generate JSON for...", extract_json=True)
        data = json.loads(json_str)
    """
    try:
        # Build command using configured CLI path
        cmd = [
            "powershell", "-ExecutionPolicy", "Bypass", "-Command",
            f"{GEMINI_CLI_COMMAND} '{prompt_text}'"
        ]
        
        print(f"[LLM Factory] Calling Gemini CLI: {GEMINI_CLI_COMMAND}")
        
        # Run subprocess with increased timeout
        result = subprocess.run(
            cmd, 
            capture_output=True, 
            text=True, 
            encoding='utf-8',
            timeout=60  # Increased from 30s to 60s
        )
        
        if result.returncode != 0:
            print(f"[LLM Factory] CLI Error (code {result.returncode}): {result.stderr}")
            return None
        
        output = result.stdout
        
        # Extract JSON from markdown if requested
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

