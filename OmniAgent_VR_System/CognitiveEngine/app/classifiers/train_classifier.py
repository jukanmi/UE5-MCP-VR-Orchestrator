"""
File: train_classifier.py
Purpose: Train KLUE-RoBERTa-small for Speak/Else intent classification.

Usage:
    cd CognitiveEngine
    python -m app.classifiers.train_classifier
"""
import json
import os
from pathlib import Path

import torch
from torch.utils.data import Dataset, DataLoader
from transformers import (
    AutoTokenizer,
    AutoModelForSequenceClassification,
    TrainingArguments,
    Trainer,
    DataCollatorWithPadding
)

# Paths
BASE_DIR = Path(__file__).parent
TRAINING_DATA_PATH = BASE_DIR / "training_data.json"
MODEL_OUTPUT_DIR = BASE_DIR / "intent_model"

# Model
MODEL_NAME = "klue/roberta-small"  # ~68M params, Korean language model

# Labels
LABEL2ID = {"speak": 0, "else": 1}
ID2LABEL = {0: "speak", 1: "else"}


class IntentDataset(Dataset):
    """Dataset for intent classification."""
    
    def __init__(self, data, tokenizer, max_length=64):
        self.data = data
        self.tokenizer = tokenizer
        self.max_length = max_length
    
    def __len__(self):
        return len(self.data)
    
    def __getitem__(self, idx):
        item = self.data[idx]
        text = item["text"]
        label = LABEL2ID[item["label"]]
        
        encoding = self.tokenizer(
            text,
            truncation=True,
            max_length=self.max_length,
            padding=False,
            return_tensors=None
        )
        
        return {
            "input_ids": encoding["input_ids"],
            "attention_mask": encoding["attention_mask"],
            "labels": label
        }


def load_training_data():
    """Load training data from JSON file."""
    if not TRAINING_DATA_PATH.exists():
        raise FileNotFoundError(f"Training data not found: {TRAINING_DATA_PATH}")
    
    with open(TRAINING_DATA_PATH, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    # Filter out example entries
    data = [item for item in data if not item["text"].startswith("예시:")]
    
    if len(data) < 10:
        raise ValueError(f"Not enough training data. Found {len(data)} entries, need at least 10.")
    
    print(f"[Train] Loaded {len(data)} training examples")
    return data


def train():
    """Train the intent classifier."""
    print("[Train] Loading training data...")
    data = load_training_data()
    
    # Split data (80% train, 20% eval)
    split_idx = int(len(data) * 0.8)
    train_data = data[:split_idx]
    eval_data = data[split_idx:]
    
    print(f"[Train] Train: {len(train_data)}, Eval: {len(eval_data)}")
    
    # Load tokenizer and model
    print(f"[Train] Loading model: {MODEL_NAME}")
    tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
    model = AutoModelForSequenceClassification.from_pretrained(
        MODEL_NAME,
        num_labels=2,
        id2label=ID2LABEL,
        label2id=LABEL2ID
    )
    
    # Create datasets
    train_dataset = IntentDataset(train_data, tokenizer)
    eval_dataset = IntentDataset(eval_data, tokenizer)
    
    # Data collator
    data_collator = DataCollatorWithPadding(tokenizer=tokenizer)
    
    # Training arguments
    training_args = TrainingArguments(
        output_dir=str(MODEL_OUTPUT_DIR),
        num_train_epochs=10,
        per_device_train_batch_size=8,
        per_device_eval_batch_size=8,
        warmup_steps=50,
        weight_decay=0.01,
        logging_dir=str(MODEL_OUTPUT_DIR / "logs"),
        logging_steps=10,
        eval_strategy="epoch",
        save_strategy="epoch",
        load_best_model_at_end=True,
        metric_for_best_model="eval_loss",
    )
    
    # Trainer
    trainer = Trainer(
        model=model,
        args=training_args,
        train_dataset=train_dataset,
        eval_dataset=eval_dataset,
        data_collator=data_collator,
    )
    
    # Train
    print("[Train] Starting training...")
    trainer.train()
    
    # Save model
    print(f"[Train] Saving model to {MODEL_OUTPUT_DIR}")
    trainer.save_model(str(MODEL_OUTPUT_DIR))
    tokenizer.save_pretrained(str(MODEL_OUTPUT_DIR))
    
    print("[Train] Training complete!")


if __name__ == "__main__":
    train()
