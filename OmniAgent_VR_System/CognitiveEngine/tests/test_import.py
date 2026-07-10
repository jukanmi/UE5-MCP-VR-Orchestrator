import sys
import traceback

from pathlib import Path
sys.path.append(str(Path(__file__).resolve().parents[1]))

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
