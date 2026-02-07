"""
File: llm_factory.py
Purpose: Centralized LLM Provider.
Supports Ollama (local), Gemini, and OpenAI models.
Ensures API Key presence via python-dotenv.
"""
import os
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
    # Google AI Studio
    "gemma": "gemma-3-27b-it",           # Gemma 3 27B Instruct
    "gemini": "gemini-2.5-flash",        # Gemini 2.5 Flash (fast)
    "gemini-3": "Gemini 3 Flash",      # Gemini 3 Flash (fast)
    # OpenAI
    "openai": "gpt-5-nano",              # GPT-5 Nano
}

# Default model - change this to switch globally
DEFAULT_MODEL = "gemini-3"  # Options: "qwen", "llama", "gemma", "gemini", "gemini-3", "openai"

# Ollama server URL (default: localhost)
OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL")

def get_llm(model_name: str = None, temperature: float = 0.0):
    """
    Returns a configured LLM instance.
    
    Args:
        model_name: Model to use. Options: "qwen", "llama", "gemma", "gemini", "gemini-3", "openai"
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
    elif model_name in ["gemma", "gemini", "gemini-3"]:
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
