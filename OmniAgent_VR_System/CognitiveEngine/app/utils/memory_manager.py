"""
File: memory_manager.py
Purpose: Dynamic conversation memory management for NPCs.
- Stores conversation history per NPC
- Saves to JSON files for persistence
- Summarizes old memories when token limit approaches
"""
import os
import json
from datetime import datetime
from typing import List, Optional, Dict
from dataclasses import dataclass, asdict
from .llm_factory import get_llm

# Token budget settings (user-specified)
MAX_TOKENS_PER_NPC = 1000  # RAG Memories budget: 500-1000 tokens
SUMMARIZE_THRESHOLD = 0.8  # Summarize at 80%
ENTRIES_TO_SUMMARIZE = 5   # Number of oldest entries to summarize at once

# Approximate tokens per character (conservative estimate)
CHARS_PER_TOKEN = 4

# Memory storage path
MEMORY_BASE_PATH = "app/agents/knowledge"

@dataclass
class MemoryEntry:
    timestamp: str
    speaker: str  # "Player" or NPC name
    content: str
    is_summary: bool = False
    
    def to_dict(self) -> dict:
        return asdict(self)
    
    @classmethod
    def from_dict(cls, data: dict) -> 'MemoryEntry':
        return cls(**data)
    
    def estimate_tokens(self) -> int:
        """Estimate token count for this entry."""
        text = f"{self.speaker}: {self.content}"
        return len(text) // CHARS_PER_TOKEN

class ConversationMemory:
    """Manages conversation memory for a single NPC."""
    
    def __init__(self, agent_id: str):
        self.agent_id = agent_id.lower()
        self.entries: List[MemoryEntry] = []
        self._load_from_file()
    
    @property
    def memory_file_path(self) -> str:
        """Path to the memory JSON file."""
        return os.path.join(
            MEMORY_BASE_PATH, 
            self.agent_id, 
            "conversation_memory.json"
        )
    
    def _load_from_file(self):
        """Load existing memories from JSON file."""
        if os.path.exists(self.memory_file_path):
            try:
                with open(self.memory_file_path, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                    self.entries = [MemoryEntry.from_dict(e) for e in data.get("entries", [])]
                print(f"[Memory] Loaded {len(self.entries)} entries for {self.agent_id}")
            except Exception as e:
                print(f"[Memory] Error loading memory for {self.agent_id}: {e}")
                self.entries = []
        else:
            print(f"[Memory] No existing memory file for {self.agent_id}")
    
    def _save_to_file(self):
        """Save memories to JSON file."""
        # Ensure directory exists
        os.makedirs(os.path.dirname(self.memory_file_path), exist_ok=True)
        
        try:
            data = {
                "agent_id": self.agent_id,
                "last_updated": datetime.now().isoformat(),
                "entries": [e.to_dict() for e in self.entries]
            }
            with open(self.memory_file_path, 'w', encoding='utf-8') as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            print(f"[Memory] Saved {len(self.entries)} entries for {self.agent_id}")
        except Exception as e:
            print(f"[Memory] Error saving memory for {self.agent_id}: {e}")
    
    def estimate_total_tokens(self) -> int:
        """Estimate total tokens used by all entries."""
        return sum(e.estimate_tokens() for e in self.entries)
    
    def add_entry(self, speaker: str, content: str):
        """Add a new conversation entry."""
        entry = MemoryEntry(
            timestamp=datetime.now().isoformat(),
            speaker=speaker,
            content=content,
            is_summary=False
        )
        self.entries.append(entry)
        print(f"[Memory] Added entry for {self.agent_id}: {speaker[:10]}...")
        
        # Check if summarization is needed
        self._check_and_summarize()
        
        # Save to file
        self._save_to_file()
    
    def _check_and_summarize(self):
        """Check token count and summarize if needed."""
        current_tokens = self.estimate_total_tokens()
        threshold = int(MAX_TOKENS_PER_NPC * SUMMARIZE_THRESHOLD)
        
        if current_tokens >= threshold:
            print(f"[Memory] Token threshold reached ({current_tokens}/{MAX_TOKENS_PER_NPC}), summarizing...")
            self._summarize_oldest_entries()
    
    def _summarize_oldest_entries(self):
        """Summarize the oldest non-summary entries using LLM."""
        # Find oldest non-summary entries
        non_summary_entries = [e for e in self.entries if not e.is_summary]
        
        if len(non_summary_entries) < ENTRIES_TO_SUMMARIZE:
            print(f"[Memory] Not enough entries to summarize ({len(non_summary_entries)})")
            return
        
        # Get entries to summarize
        entries_to_summarize = non_summary_entries[:ENTRIES_TO_SUMMARIZE]
        
        # Build conversation text
        conversation_text = "\n".join([
            f"{e.speaker}: {e.content}" for e in entries_to_summarize
        ])
        
        try:
            # Use LLM to summarize
            llm = get_llm(temperature=0.3)
            
            summary_prompt = f"""Summarize the following conversation between Player and {self.agent_id} into a brief, third-person narrative. 
Keep important facts, events, and emotional context. Maximum 2-3 sentences.

Conversation:
{conversation_text}

Summary:"""
            
            response = llm.invoke(summary_prompt)
            summary_text = response.content if hasattr(response, 'content') else str(response)
            
            # Create summary entry
            summary_entry = MemoryEntry(
                timestamp=datetime.now().isoformat(),
                speaker="[Summary]",
                content=summary_text.strip(),
                is_summary=True
            )
            
            # Remove old entries and add summary
            for entry in entries_to_summarize:
                self.entries.remove(entry)
            
            # Insert summary at the beginning (oldest position)
            self.entries.insert(0, summary_entry)
            
            print(f"[Memory] Summarized {len(entries_to_summarize)} entries into 1 summary")
            
        except Exception as e:
            print(f"[Memory] Error summarizing: {e}")
    
    def get_recent_entries(self, k: int = 5) -> List[MemoryEntry]:
        """Get the k most recent entries."""
        return self.entries[-k:] if self.entries else []
    
    def get_context_string(self, k: int = 5) -> str:
        """Get recent memories as a formatted string for LLM context."""
        recent = self.get_recent_entries(k)
        if not recent:
            return ""
        
        lines = []
        for entry in recent:
            if entry.is_summary:
                lines.append(f"[Past events] {entry.content}")
            else:
                lines.append(f"{entry.speaker}: {entry.content}")
        
        return "\n".join(lines)


# Cache for loaded memories
_memory_cache: Dict[str, ConversationMemory] = {}

def get_memory(agent_id: str) -> ConversationMemory:
    """Get or create memory manager for an NPC."""
    agent_lower = agent_id.lower()
    if agent_lower not in _memory_cache:
        _memory_cache[agent_lower] = ConversationMemory(agent_id)
    return _memory_cache[agent_lower]

def add_conversation(agent_id: str, player_input: str, npc_response: str):
    """Add a conversation turn to NPC's memory."""
    memory = get_memory(agent_id)
    memory.add_entry("Player", player_input)
    memory.add_entry(agent_id, npc_response)

def get_conversation_context(agent_id: str, k: int = 5) -> str:
    """Get recent conversation context for an NPC."""
    memory = get_memory(agent_id)
    return memory.get_context_string(k)

def clear_memory_cache():
    """Clear the memory cache."""
    global _memory_cache
    _memory_cache.clear()
