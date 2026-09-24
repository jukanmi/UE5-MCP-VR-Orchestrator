---
trigger: model_decision
description: 커밋·브랜치·PR 작업 시
---

# Git 커밋·브랜치 규칙 (git.md)

**브랜치**: `main`(릴리스 전용, 직접 커밋·force-push 금지) · `Develop`(통합) · `feature/*` · `bugfix/*` · `refactor/*`.

**메시지**: `<type>: <제목 50자↓> — <변경 시스템·파일>`. type: `feat` `fix` `refactor` `chore` `perf` `test`.

**단위**: 한 커밋 = 한 논리 변경. `.uasset` 은 관련 C++ 와 같은 커밋. WIP 커밋 금지.

**타이밍**: 기능 마일스톤·버그 패치 검증 완료 직후 커밋. `git push` 는 명시적 지시 없이 금지.

**PR**: `feature/*` → `Develop`. 본문: 변경 이유·테스트 방법·체크리스트. `main` 직접 머지 금지.
