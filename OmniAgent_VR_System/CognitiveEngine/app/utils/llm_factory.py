"""
File: llm_factory.py
Purpose: Centralized LLM Provider.
Returns configured ChatOpenAI instance (gpt-4o-mini).
Ensures API Key presence via python-dotenv.
"""
import os
from dotenv import load_dotenv
from langchain_openai import ChatOpenAI
from langchain_google_genai import ChatGoogleGenerativeAI

# Load .env from root of CognitiveEngine if mostly run from there, 
# or rely on system env vars.
load_dotenv()

def get_llm(model_provider="google", temperature=0.0):
    """
    Returns a configured LLM instance.
    - model_provider="google": Uses Gemini/Gemma models (via ChatGoogleGenerativeAI).
    - model_provider="openai": Uses GPT-5 Nano (via ChatOpenAI).
    """
    if model_provider == "google":
        # Google GenAI (Gemma/Gemini)
        # Note: Ensure langchain-google-genai is installed and GOOGLE_API_KEY is set.
        # "gemini-2.0-flash" is a good placeholder for high-performance efficient models.
        return ChatGoogleGenerativeAI(
            model="gemini-2.5-flash", 
            temperature=temperature,
            google_api_key=os.getenv("GOOGLE_API_KEY"),
            convert_system_message_to_human=True
        )
    
    elif model_provider == "openai":
        # OpenAI (GPT-5 Nano)
        return ChatOpenAI(
            model="gpt-5-nano",
            temperature=temperature,
            api_key=os.getenv("OPENAI_API_KEY"),
            max_retries=2
        )
    
    else:
        raise ValueError(f"Unknown model_provider: {model_provider}")
