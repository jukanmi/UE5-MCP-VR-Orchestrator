"""
File: graph.py
Purpose: LangGraph Definition.
Constructs the StateGraph that connects Supervisor -> Dialogue / Rules.
Defines the entry point and conditional edges for the multi-agent workflow.
"""
from langgraph.graph import StateGraph, END
from .agents.state import AgentState
from .agents.supervisor import supervisor_node, should_continue
from .agents.subgraphs.dialogue import dialogue_node
from .agents.subgraphs.rules import rules_node
from .agents.interface import interface_node

# Define Graph
workflow = StateGraph(AgentState)

# Add Nodes
# Add Nodes
workflow.add_node("Interface", interface_node)
workflow.add_node("Supervisor", supervisor_node)
workflow.add_node("Dialogue", dialogue_node)
workflow.add_node("Rules", rules_node)

# Add Edges
workflow.set_entry_point("Interface")
workflow.add_edge("Interface", "Supervisor")

workflow.add_conditional_edges(
    "Supervisor",
    should_continue,
    {
        "Dialogue": "Dialogue",
        "Rules": "Rules",
        "End": END
    }
)

# Agents return to Supervisor to allow fallback logic or aggregation
workflow.add_edge("Rules", "Supervisor")
workflow.add_edge("Dialogue", END) # Dialogue ends the turn

# Compile
app_graph = workflow.compile()
