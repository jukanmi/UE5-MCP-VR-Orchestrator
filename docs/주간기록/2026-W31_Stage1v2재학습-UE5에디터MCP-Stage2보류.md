# W31 (2026-07-27 ~ 08-02) — Stage1 v2 재학습 · UE5 에디터 MCP 연결 · Stage2 보류 확정

<!-- Memo.md Done(07-31 3건) + 커밋 4건 기반 소급 작성(2026-08-29). -->

## 핵심

세 갈래가 같은 날(07-31) 매듭지어진 주. ① **Stage1 v2 재학습 완주** — v1 의 overfit 우려를 데이터 결함 청산으로 풀었다. 특히 `수리`→`복원` 전역치환이 `독수리`를 `독복원`으로 깨뜨린 7곳처럼, 눈으로 안 보면 못 잡는 결함들이 있었다. ② **UE5 에디터 MCP 연결** — 기존 `RemoteControl` 플러그인의 raw HTTP API 를 프록시하는 방식이라 UE 측 수정도 신규 파이썬 의존성도 0. 연결하자마자 **문서 오기를 적발**한 게 첫 성과였다. ③ **Stage2(12B plan) 보류 확정** — 데이터나 설정 문제가 아니라 상류 지원 공백이라는 판단.

MCP 연결의 의미가 컸다. 그동안 "에디터에 배치된 가구가 3개"라고 문서에 적혀 있었는데 실측하니 **침대 1개뿐**이었다. 문서와 실물이 갈라져 있어도 확인할 방법이 없던 상태에서, 이제 코드가 에디터를 직접 조회할 수 있게 됐다.

## 주요 작업

### Stage1 재학습 v2 (07-31, `ba4626d`·`a76e5d8`·`99d06b6`)

- **데이터 결함 3종 청산**:
  - `수리`→`복원` 전역치환이 `독수리`를 `독복원`으로 깬 것 **7곳**.
  - 서빙에 존재하지 않는 sentiment `Enemy` 태그 **12곳** → `Hostile`.
  - `lifestyle_pray_01` 의 gold 가 utterance 와 무관(Angry/TurnTo+Scan) → `Pray/Altar_01`.
- **앵무새 가드**(`is_parrot`) — teacher 가 플레이어 발화를 NPC 대사로 그대로 복창한 행 13/707(1.8%). 드롭 대신 temp 0.6→0.95 로 재굴림. 재생성 711행, 복창 잔존 **0**.
- **>1024 토큰 10행 제거** — 잘림이 시퀀스 끝(= assistant JSON 학습 타겟)을 자르므로 **깨진 JSON 을 정답으로 가르치는** 상태였다. 최종 701행.
- **학습**: 528스텝 3ep, loss 0.408 → 0.043. GGUF Q4_K_M → ollama `gemma4-e4b-dialogue-v2` 등록(v1 파라미터 상속 — `RENDERER/PARSER gemma4`, temp 1 · top_k 64 · top_p 0.95).
- **eval**(`broad_result_v2.json`, v1 대비): gold 재현 6/8 → **7/8**, held-out 정상 액션 0 → **3건**. v1 이 내던 bare string 포맷 깨짐(`['S','l','e','e','p']`)이 v2 에서 해소.

### UE5 에디터 MCP 서버 (07-31 연결 / 08-02 커밋 `f98cd40`)

- `tools/ue_mcp/server.py` — 툴 4개(call · get · set · search_assets) + `.mcp.json` 의 `ue5` 항목.
- 기존 `RemoteControl` 플러그인(uproject 에 이미 활성)의 raw HTTP API `127.0.0.1:30010` 프록시 → **UE 측 수정 0, 신규 파이썬 의존성 0**.
- 실연결 검증: `GetAllLevelActors`(86개) · `ListAssets` · `K2_GetActorLocation` · `GetSelectedLevelActors` · `GetCurrentLevel` · 프로퍼티 읽기 전부 동작. 쓰기(`ue_set_property`)만 이 시점 미검증.
- **첫 성과 = 문서 오기 적발**: 가구 배치가 `1001`(의자)·`1002`(의자)·`Bed_01` 3개로 기재돼 있었으나 실측은 **침대 1개(ID `1002`)뿐**. 의자 액터는 제거된 상태였다. DoList·주간기록 W29 정정.
  - 후속(08-18): 이 삭제는 **의도된 것**이었다 — "누워" 지시가 침대를 고르는지 보려고 의자를 후보에서 뺀 것. "워킹트리에서 삭제(커밋 안 됨)" 도 오기 — 추가 `1e72f21`, 삭제 `b5675ef` 로 이미 커밋돼 있었다.

### Stage2(12B plan) 보류 확정 (07-31)

데이터·설정이 아니라 **상류 지원 공백**이 원인이라는 판단. 상세 배경은 `Memo.md` Handoff Notes.

## 메모

- **eval 이 실환경과 다른 ID 로 돈다** — 학습·eval 프롬프트(`finetune/eval/*.py`, `build_dataset_from_raw_lorebooks.py`)는 여전히 `Bed_01`·`1001` 을 예시 ID 로 쓴다. 모델이 컨텍스트 제공 ID 를 복사하도록 학습돼 런타임 영향은 없지만, eval 만 놓고 보면 실환경과 다른 세계를 테스트하는 셈.
- **v2 도 MODELS 스위치는 보류** — PIE 확인 후로 미뤘다. 롤백은 문자열 1줄이라 위험은 낮다.

## 커밋

| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 07-31 | `99d06b6` | feat: 파인튜닝 학습·배포·eval 스크립트 추적 — train·deploy·eval·config |
| 07-31 | `a76e5d8` | feat: Stage1 앵무새 가드·동적 오버샘플 — generate_stage1·generate_stage2 |
| 07-31 | `ba4626d` | fix: 학습 시드 데이터 결함 3종 — golden_plan_seed_light·scenarios_seed_draft |
| 08-02 | `f98cd40` | feat: UE5 에디터 MCP 서버 — tools/ue_mcp |
