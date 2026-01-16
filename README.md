UE5-MCP-VR-Orchestrator
A Multi-Agent LLM Orchestration Framework for Unreal Engine 5 VR Games powered by Model Context Protocol (MCP) and LangGraph.


📖 Overview
This repository implements a novel architecture for immersive VR games where Non-Player Characters (NPCs) and game worlds are driven by Large Language Models (LLMs). Unlike simple chatbots, this system solves the "Disconnected Model Problem" by orchestrating four specialized agents via the Model Context Protocol (MCP).

It bridges the gap between probabilistic AI (LLMs) and deterministic game engines (UE5) using a Hybrid Input Pipeline (Voice + Semantic Gestures) and LangGraph state machines.

🏗 Architecture
The system is built on a "Mixture of Experts" (MoE) pattern, orchestrating four distinct agents:

Narrative Agent (World Manager): Manages dynamic storytelling, RAG-based lore retrieval, and scene direction.

Character Agent (Actor): Handles NPC persona, emotions, memory, and TTS/Audio2Face synchronization.

Rules Agent (Referee): A deterministic logic engine that validates actions against game rules and calculates outcomes (e.g., dice rolls, inventory checks) returning strict JSON.

Interface Agent (Mediator): Interprets multimodal VR inputs (Voice + Gesture Tags) into semantic intent.


Data Flow
VR Input (Voice/Gesture) → UE5 Pre-processing → MCP Client → LangGraph Router → Specialized Agents → JSON Output → UE5 Blackboard → Behavior Tree Execution


✨ Key Features
MCP Integration: Standardized protocol for exposing UE5 state (Health, Inventory, World State) as AI context resources.

Semantic Gesture Recognition: Converts raw VR motion controller data into semantic tags (e.g., Gesture:Wave, Action:Point + Target:Door) before sending to LLM.

Deterministic Rule Enforcement: Prevents AI hallucinations by forcing rule-based logic to validate game state changes via MCP Tools.

Async Behavior Tree: Custom BTTask_AsyncLLM nodes that handle streaming responses without blocking the game thread, utilizing filler animations for latency masking.

Low Latency Pipeline: Parallel processing of logic and audio generation (TTS) with NVIDIA Audio2Face integration.


🛠 TechStack
Host Engine: Unreal Engine 5.5+ (C++ & Blueprints)

MCP Server: Python 3.12, FastMCP

Orchestration: LangGraph, LangChain

LLM Provider: Claude 3.5 Sonnet (Recommended) or GPT-4o

VR Inputs: Meta XR Plugin / VR Expansion Plugin (for gesture tagging)

Lip Sync: NVIDIA Audio2Face / OVR LipSync


🚀 Getting Started
Prerequisites
Unreal Engine 5.5 or later

Python 3.12+ installed and added to PATH

Visual Studio 2022 (for C++ compilation)

API Keys for Anthropic/OpenAI and ElevenLabs (optional)

Installation
1. Clone the Repositorybash git clone https://github.com/jukanmi/UE5-MCP-VR-Orchestrator.git

2. Python Environment Setup Navigate to the Server directory and install dependencies:

```Bash
cd Server
python -m venv venv
source venv/bin/activate  # or venv\Scripts\activate on Windows
pip install -r requirements.txt
Unreal Plugin Setup
```

3. Unreal Plugin Setup

    Copy the Plugins/UE5_MCP_Bridge folder into your project's Plugins directory.
  
    Regenerate Visual Studio project files and compile.

    Enable the plugin in Edit > Plugins.

4. Configuration

    Create a .env file in the Server directory with your API keys:

```코드 스니펫
ANTHROPIC_API_KEY=sk-ant-...
ELEVENLABS_API_KEY=...
Usage
Start the MCP Server:
```

Usage

1. Start the MCP Server:

```Bash
python server/main.py
```

2. Open your Unreal Engine project.

3. Ensure the MCP Subsystem is connected (check Output Log).

4. Play in VR Preview.

## 📂 Project Structure

```directory
UE5-MCP-VR-Orchestrator/
├── Content/                # UE5 Assets (Blueprints, Maps)
├── Plugins/
│   └── UE5_MCP_Bridge/     # C++ Plugin for WebSocket/MCP communication
├── Server/
│   ├── agents/             # LangGraph Agent definitions
│   │   ├── narrative.py
│   │   ├── character.py
│   │   ├── rules.py
│   │   └── interface.py
│   ├── tools/              # MCP Tools (Dice, Inventory, Quest)
│   ├── main.py             # Entry point
│   └── requirements.txt
├── Config/                 # Agent Personas and Rulesets (JSON/YAML)
└── README.md
```

🗓 Roadmap
[ ] Phase 1: Basic WebSocket connection between UE5 and Python MCP Server.

[ ] Phase 2: Implement "Rules Agent" with JSON-structured output for inventory management.

[ ] Phase 3: Integrate VR Gesture semantic tagging (Pointing/Grabbing).

[ ] Phase 4: LangGraph orchestration of the 4-agent loop.

[ ] Phase 5: Optimization (Streaming TTS & Latency Masking).

🤝 Contributing
Contributions are welcome! Please check the CONTRIBUTING.md for guidelines on how to submit Pull Requests.

📄 License
This project is licensed under the  License - see the(LICENSE) file for details.

Note: This is an experimental project exploring the intersection of Generative AI and Game Development. Performance may vary based on hardware and API latency.
