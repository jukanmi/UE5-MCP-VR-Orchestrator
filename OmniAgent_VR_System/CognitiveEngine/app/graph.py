"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: graph.py                                                              ║
║ Role: LANGGRAPH WORKFLOW DEFINITION (Pipeline Structure)                   ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Define the execution graph connecting all agents. Specifies node order,   ║
║   conditional routing, and entry/exit points.                               ║
║                                                                              ║
║ PIPELINE ARCHITECTURE (Section 8 Orchestra):                                ║
║                                                                              ║
║   Entry → Interface_Input → Supervisor → Dialogue                          ║
║                                  ↓             ↓                            ║
║                              Supervisor → Interface_Output                  ║
║                                  ↓             ↓                            ║
║                              Supervisor →   Rules → Supervisor              ║
║                                                         ↓                   ║
║                       END (정상/폴백) 또는 Dialogue (거부 1회 재시도)        ║
║                                                                              ║
║ NODES:                                                                       ║
║   • Interface_Input:  UE5 data → natural language                           ║
║   • Supervisor:       Central orchestrator (routing hub)                    ║
║   • Dialogue:         NPC response generation                               ║
║   • Interface_Output: Natural language → ActionBatch                        ║
║   • Rules:            Validation and clamping                               ║
║                                                                              ║
║ ROUTING MECHANISM:                                                           ║
║   - All agents return to Supervisor for next-step decision                  ║
║   - Supervisor uses should_continue() based on current_speaker              ║
║   - Conditional edges enable dynamic flow control                           ║
║                                                                              ║
║ MODIFICATION POLICY:                                                         ║
║   - Edge changes require updating supervisor.py routing logic               ║
║   - New nodes require new conditional routes in should_continue()           ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""

from langgraph.graph import StateGraph, END
from .agents.state import AgentState
from .agents.supervisor import supervisor_node, should_continue
from .agents.interface_input import interface_input_node
from .agents.interface_output import interface_output_node
from .agents.subgraphs.dialogue import dialogue_node
from .agents.subgraphs.rules import rules_node

# Define Graph
workflow = StateGraph(AgentState)

# Add Nodes (Section 8 Orchestra)
workflow.add_node("Interface_Input", interface_input_node)
workflow.add_node("Supervisor", supervisor_node)
workflow.add_node("Dialogue", dialogue_node)
workflow.add_node("Interface_Output", interface_output_node)
workflow.add_node("Rules", rules_node)

# Entry Point: Interface Input
workflow.set_entry_point("Interface_Input")

# Flow: Interface Input → Supervisor (for routing)
workflow.add_edge("Interface_Input", "Supervisor")

# Supervisor routes conditionally based on current_speaker
workflow.add_conditional_edges(
    "Supervisor",
    should_continue,
    {
        "Interface_Input": "Interface_Input",
        "Dialogue": "Dialogue",
        "Interface_Output": "Interface_Output",
        "Rules": "Rules",
        "End": END,
    },
)

# Each agent returns to Supervisor for orchestration
workflow.add_edge("Dialogue", "Supervisor")
workflow.add_edge("Interface_Output", "Supervisor")
# Rules 도 Supervisor 경유 — 거부(전 액션 기각) 시 1회 재시도/폴백 판정을 Supervisor 가 수행.
# (과거 Rules→END 직결로 supervisor 4단계 재시도 분기가 데드코드였음)
workflow.add_edge("Rules", "Supervisor")

# Compile
app_graph = workflow.compile()
