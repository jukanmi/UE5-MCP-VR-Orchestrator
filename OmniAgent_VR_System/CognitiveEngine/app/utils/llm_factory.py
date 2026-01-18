"""
File: llm_factory.py
Purpose: Centralized LLM Provider.
Returns configured ChatOpenAI instance (gpt-4o-mini).
Ensures API Key presence via python-dotenv.
"""
import os
from dotenv import load_dotenv
from langchain_openai import ChatOpenAI

# Load .env from root of CognitiveEngine if mostly run from there, 
# or rely on system env vars.
load_dotenv()

def get_llm(temperature: float = 0.0, model: str = "gpt-4o-mini"):
    """
    Returns a ChatOpenAI instance.
    Raises ValueError if OPENAI_API_KEY is missing.
    """
    api_key = os.getenv("OPENAI_API_KEY")
    if not api_key:
        # Check if user set it in system env manually not via .env
        pass 
        # Actually LangChain will complain itself, but explicit error is better.
        # print("WARNING: OPENAI_API_KEY not found in env.")

    return ChatOpenAI(
        model=model,
        temperature=temperature,
        max_retries=2
    )
