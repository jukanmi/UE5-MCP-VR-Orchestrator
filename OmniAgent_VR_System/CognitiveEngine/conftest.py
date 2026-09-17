"""pytest sys.path 앵커 — `import app` 을 실행 방식과 무관하게 보장.

`python -m pytest` 는 cwd 를 sys.path 에 넣지만 bare `pytest`(CI)는 안 넣음 →
CI 에서 ModuleNotFoundError: 'app' (2026-07-10 첫 Actions 실행 실사고).
일부 테스트만 통과해 보이던 것은 test_ws_roundtrip 등이 모듈 import 시
sys.path 를 append 하던 부수효과 — 수집 순서 의존이라 신뢰 불가.
이 conftest 가 CognitiveEngine 루트를 명시 삽입해 결정적으로 고정한다.
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# 유닛 테스트가 실제 app/story/content + story_state.json 을 건드리고 디렉터 LLM 을 실호출하는 것 차단.
# 스토리 테스트는 set_story(machine) 으로 임시 상태기계를 명시 주입한다(test_story_director.py 참조).
os.environ.setdefault("STORY_ENABLED", "0")
