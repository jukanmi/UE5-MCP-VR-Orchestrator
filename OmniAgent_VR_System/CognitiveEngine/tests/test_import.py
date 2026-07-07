import sys
import traceback

sys.path.append('c:/github/UE5_MCP_VR/OmniAgent_VR_System/CognitiveEngine')

try:
    from app.agents.interface_output import interface_output_node
    print('Import interface_output_node Success!')
except Exception as e:
    print('Error importing interface_output_node:')
    traceback.print_exc()

try:
    import app.main
    print('Import app.main Success!')
except Exception as e:
    print('Error importing app.main:')
    traceback.print_exc()
