<!--
File: README.md
Purpose: Project Documentation.
Overview of the OmniAgent VR System, Architecture (Remote Cortex), and Implementation Status.
-->

# OmniAgent VR System

A Multi-Agent LLM Orchestration Framework for Unreal Engine 5 VR Games, powered by **Model Context Protocol (MCP)** and **LangGraph**.

## 📖 Overview

This project implements the **Remote Cortex Pattern**, where the heavy lifting of reasoning and state management is offloaded to a Python-based **Cognitive Engine**, while **Unreal Engine 5** focuses on rendering and immersive VR interaction.

The system orchestrates four specialized agents to create a dynamic, living world:

1. **Supervisor (Narrative)**: Manages global plot and delegates tasks.
2. **Dialogue (Character)**: Generates persona-driven conversations with long-term memory.
3. **Rules (Referee)**: Enforces game logic with deterministic validation (Clamping).
4. **Interface (Mediator)**: Interprets multimodal inputs (Voice + Gesture) using **GesPrompt**.

## 🏗 Architecture

### Remote Cortex Pattern

- **Cognitive Engine (Python)**: Handles all high-level logic, state management, and LLM inferences.
- **Unreal Project (C++)**: Acts as a "Body", sending sensor data (Voice, Gaze, Gestures) and executing actions.
- **Communication**: Asynchronous WebSocket connection for real-time performance.

### Directory Structure

```directory
/OmniAgent_VR_System
├── 📂 CognitiveEngine               # Python-based Agent Server (FastAPI + LangGraph)
│   ├── 📂 app
│   │   ├── 📂 agents                # Agent Logic (Supervisor, Dialogue, Rules)
│   │   ├── 📂 schemas               # [Strict Data Contracts] Pydantic V2
│   │   │   ├── 📄 actions.py         # ActionBatch with Clamp Logic
│   │   │   ├── 📄 game_state.py      # Vector3D & Entity Validation
│   │   │   └── 📄 vr_context.py      # GesPrompt (Voice + Gesture)
│   │   └── 📄 main.py                # WebSocket Entry Point
│   └── 📄 requirements.txt
├── 📂 UnrealProject                 # UE5 Simulation Environment
│   └── 📂 Source/Network            # C++ WebSocket Client
└── 📂 SharedData                    # Common Constants
```

## ✨ Key Technical Features

### 1. Strict Data Contracts (Pydantic V2)

To prevent LLM hallucinations from crashing the game engine, all agent outputs are strictly validated on the Python side.

- **Clamping**: Damage values > 100 are automatically clamped.
- **Validation**: `Vector3D` coordinates must be finite (no NaN/Inf).
- **ActionBatch**: Ensures all actions sent to UE5 are well-formed.

### 2. GesPrompt (Multimodal Interface)

The **Interface Agent** fuses voice transcriptions with VR gesture data to resolve deictic references.

- _Player says_: "Open **this**." + _Gaze/Point_: [Door_42]
- _System interprets_: `Intent: Open(Target=Door_42)`

### 3. Agent Orchestration

- **Supervisor**: The "Brain" that decides which agent should handle the current request.
- **Dialogue**: Uses `memory_summary` in Persona YAMLs to remember past interactions.
- **Rules**: Validates actions against game rules before they happen.

## Getting Started (Phase 1: Foundation)

### Prerequisites

- Python 3.10+
- Unreal Engine 5.5+
- `pip install -r CognitiveEngine/requirements.txt`

### Running the Cognitive Engine

```bash
cd OmniAgent_VR_System/CognitiveEngine
python -m uvicorn app.main:app --port 8000
```

_Server will start at `ws://localhost:8000/ws/ue5`_

### Unreal Engine Setup

1. Open `UnrealProject`
2. Ensure `WebSockets` plugin is enabled.
3. The `WebSocketClient` class handles connection to the Python server.

## � Roadmap

- [x] **Phase 1: Foundation** (Monorepo, Schemas, Basic Network)
- [ ] **Phase 2: Agent Logic** (LangGraph Implementation, Persona Loading)
- [ ] **Phase 3: Integration** (UE5 Behavior Trees, Audio2Face)
- [ ] **Phase 4: Optimization** (Streaming Responses, Latency Masking)
