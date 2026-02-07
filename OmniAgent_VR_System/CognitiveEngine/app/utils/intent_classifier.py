"""
File: intent_classifier.py
Purpose: Classify user input as 'speak' or 'else' using trained KLUE-RoBERTa-small model.
"""
import os
from pathlib import Path
from typing import Literal

import torch
from transformers import AutoTokenizer, AutoModelForSequenceClassification

# Model path
MODEL_DIR = Path(__file__).parent.parent / "classifiers" / "intent_model"

# Labels
ID2LABEL = {0: "speak", 1: "else"}

# Cached model and tokenizer
_model = None
_tokenizer = None
_device = None


def _load_model():
    """Load the trained model (lazy loading with cache)."""
    global _model, _tokenizer, _device
    
    if _model is not None:
        return _model, _tokenizer, _device
    
    if not MODEL_DIR.exists():
        print("[Classifier] Model not found, returning default 'speak'")
        return None, None, None
    
    print(f"[Classifier] Loading model from {MODEL_DIR}")
    
    _device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    _tokenizer = AutoTokenizer.from_pretrained(str(MODEL_DIR))
    _model = AutoModelForSequenceClassification.from_pretrained(str(MODEL_DIR))
    _model.to(_device)
    _model.eval()
    
    print(f"[Classifier] Model loaded on {_device}")
    return _model, _tokenizer, _device


def classify_intent(text: str) -> Literal["speak", "else"]:
    """
    Classify user input as 'speak' (dialogue) or 'else' (action/command).
    
    Args:
        text: User input text
    
    Returns:
        'speak' for dialogue-type inputs
        'else' for action/command-type inputs
    """
    model, tokenizer, device = _load_model()
    
    # Fallback if model not loaded
    if model is None:
        return "speak"
    
    # Tokenize
    inputs = tokenizer(
        text,
        truncation=True,
        max_length=64,
        return_tensors="pt"
    ).to(device)
    
    # Inference
    with torch.no_grad():
        outputs = model(**inputs)
        logits = outputs.logits
        predicted_id = torch.argmax(logits, dim=-1).item()
    
    result = ID2LABEL[predicted_id]
    print(f"[Classifier] '{text[:30]}...' -> {result}")
    
    return result


def get_confidence(text: str) -> tuple[str, float]:
    """
    Classify with confidence score.
    
    Returns:
        Tuple of (label, confidence)
    """
    model, tokenizer, device = _load_model()
    
    if model is None:
        return "speak", 0.5
    
    inputs = tokenizer(
        text,
        truncation=True,
        max_length=64,
        return_tensors="pt"
    ).to(device)
    
    with torch.no_grad():
        outputs = model(**inputs)
        probs = torch.softmax(outputs.logits, dim=-1)
        predicted_id = torch.argmax(probs, dim=-1).item()
        confidence = probs[0][predicted_id].item()
    
    return ID2LABEL[predicted_id], confidence
