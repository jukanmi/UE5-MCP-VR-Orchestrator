---
name: review-changes
description: Perform a structured code review using change detection and impact
---

## Review Changes

Perform a risk-aware code review using the knowledge graph.

### Steps

1. Run `detect_changes` to get risk-scored change analysis.
2. Run `get_affected_flows` to find impacted execution paths.
3. For each high-risk function, run `query_graph` with pattern="tests_for" to check test coverage.
4. Run `get_impact_radius` to understand the blast radius.
5. For any untested changes, suggest specific test cases.
6. SOLID 점검 (변경된 파일·클래스만 대상, 기존 위반은 지적하지 않음):
   - **S 단일 책임**: 변경 후 클래스가 서로 다른 도메인 함수를 묶는지. 파일이 800줄을 넘으면 분리 검토.
   - **O 개방-폐쇄**: 신규 타입 추가가 기존 `switch(EAction)` 등 분기 수정을 요구하는지. 요구하면 핸들러 맵 제안.
   - **L 리스코프 치환**: 오버라이드가 부모 계약을 지키는지. 빈 구현·예외적 동작 `override` 는 위반 후보.
   - **I 인터페이스 분리**: 인터페이스 함수 5개 초과, 또는 구현체가 쓰지 않는 함수 존재 시 위반 후보.
   - **D 의존성 역전**: 모듈 경계(NPC↔Player↔UI)를 넘는 구체 클래스 직접 `Cast`·`GetSubsystem<>` 이 새로 생기면 위반 후보.
   - 위반 후보마다 파일:줄, 위반 원칙, 최소 수정안을 적는다. 확실하지 않으면 "후보"로 표시.

### Output Format

Provide findings grouped by risk level (high/medium/low) with:
- What changed and why it matters
- Test coverage status
- SOLID 점검 결과 (원칙별 OK / 위반 후보 / 해당 없음)
- Suggested improvements
- Overall merge recommendation

## Token Efficiency Rules
- Start with `get_minimal_context(task="<your task>")` before any other graph tool.
- Use `detail_level="minimal"` on all calls. Only escalate to "standard" when minimal is insufficient.
