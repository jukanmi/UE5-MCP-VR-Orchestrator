---
name: feature-spec
description: 큰 기능 시작 전 docs/SPEC_<feature>.md 작성 후 /clear로 깨끗한 세션에서 실행
---

## Spec

멀티파일·고위험·다세션 기능을 시작하기 전 사양서를 작성합니다.
SPEC.md를 만든 뒤 `/clear`로 컨텍스트를 비우고 새 세션에서 파일을 읽고 구현합니다.

### 사용법

```
/feature-spec <feature>
```

예: `/feature-spec tts-m2` → `docs/SPEC_tts-m2.md` 생성

### 실행 순서

1. **인터뷰** — `AskUserQuestion`으로 아래 항목을 한 번에 묶어 질문
   - 목표: 무엇을 달성하는가
   - 범위: 어떤 파일·시스템을 건드리는가
   - 결정 사항: 포맷·인코딩·라이브러리·에러 처리 등 선택지 (AGENTS.md §4 Ask-Before-Choose)
   - 완료 기준: 무엇이 되면 "완료"인가
   - 단계 분리: Phase 1/2/3 또는 M1/M2/M3 구분이 필요한가

   **반복 질문**: 답변을 받은 뒤 새로 생긴 분기·모호함·충돌이 있으면 다시 `AskUserQuestion` 으로 묻는다.
   미결 분기가 없을 때까지 반복하고, 그 전엔 SPEC 을 쓰지 않는다. 코드·문서에서 확인 가능한 사실은 묻지 말고 먼저 찾아본다.

2. **SPEC 파일 작성** — `docs/SPEC_<feature>.md` 생성

   ```markdown
   # SPEC: <feature>

   ## 목표
   ## 범위 (변경 파일·시스템)
   ## 결정 사항
   ## 완료 기준
   ## 단계 (Phase/Milestone)
   ## 미결 사항 (작업 중 발견 시 추가)
   ```

3. **컨텍스트 정리 안내** — 작성 완료 후 사용자에게 안내:
   > "SPEC 작성 완료. `/clear` 후 새 세션에서 `@docs/SPEC_<feature>.md` 로 시작하십시오."

### 규칙

- SPEC 파일은 `docs/` 에 저장 (`docs/` 는 git 추적 — 공개 저장소이므로 민감 정보 금지)
- SPEC 은 의도·결정 사항만. 구현 세부는 쓰지 않음 (코드가 설명)
- 작업 중 새 결정이 생기면 "미결 사항" 섹션에 추가
- 기능 완료 후 SPEC 파일은 삭제하거나 `docs/주간기록/` 로 이관
