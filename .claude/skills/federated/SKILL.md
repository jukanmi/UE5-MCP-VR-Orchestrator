---
name: federated
description: SPEC 한 항목을 Master(메인 세션)→Dev(Sonnet 서브에이전트)→QA(sol_pi verify + 리뷰)→승인 루프로 구현. 명시 호출 전용
disable-model-invocation: true
---

## Federated (서브에이전트 판)

`tools/federated_orchestrator.py` 의 루프를 Gemini 없이 Claude 서브에이전트로 돌린다.
역할: **Master = 이 세션**, **Dev = `Agent(model: "sonnet")`**, **QA = `sol_pi verify` + Sonnet 리뷰어 서브에이전트**.
(같은 모델 계열이라 리뷰 독립성은 Dev 와 컨텍스트를 분리하는 것으로만 확보한다.)

### 사용법

```
/federated docs/SPEC_xxx.md [추가 지시]
```

### 실행 순서

1. **준비** — 격리 작업공간을 만든다. 현재 트리에 미커밋 변경이 있으면 반드시 워크트리.
   - `task_id = <SPEC 이름에서 SPEC_ 뺀 것>-<MMDDHHmm>`
   - `git worktree add -b fed/<task_id> C:\github\UE5_MCP_VR_wt_<task_id> HEAD`
   - 워크트리는 HEAD 기준이라 SPEC 이 커밋돼 있어야 한다. 안 돼 있으면 중단하고 알린다.
   - C++ 대상이면 첫 빌드가 전체 컴파일이라 오래 걸린다. 에디터 실행 중이고 워크트리가 아니면 중단.
2. **Master 계획** — `AGENTS.md` 와 필요한 `.agents/rules/*.md` 를 읽고 SPEC 에서 **작업 하나**만 고른다.
   고르기 전에 대상 파일을 실제로 읽어 이미 구현된 항목은 제외(체크박스는 늦게 갱신됨).
   TaskSpecification 을 사용자에게 보여 주고 진행 여부를 확인한다(Ask-Before-Choose).
   - `goal`(한 문장) · `target_domain`(UE5_CPP|PYTHON_BACKEND|ASSET_3D) · `target_files` · `rule_files` · `interface_contract`
3. **Dev** — `Agent(subagent_type: "general-purpose", model: "sonnet")`. 프롬프트에 넣을 것:
   - 작업공간 절대경로(워크트리) 와 "그 경로 밖은 건드리지 말 것"
   - TaskSpecification 전체, `AGENTS.md` 와 `rule_files` 를 먼저 읽고 따를 것
   - 빌드·테스트·git commit/push 금지(QA 가 한다), 끝나면 변경 요약 3~5줄
   - 재작업이면 직전 QA 반려 사유를 맨 위에
4. **변경 확인** — 워크트리에서 `git status --porcelain` · `git add -N .` · `git diff --stat`.
   Dev 자기보고가 아니라 git 이 기준. 변경이 없으면 "실제로 구현하라" 로 재작업(횟수에 포함).
5. **QA**
   - 리뷰어: **새 컨텍스트의 Sonnet 서브에이전트**(`Agent(model: "sonnet", effort: "low")`, 읽기 전용 — Edit/Write 금지).
     `target_domain` 이 UE5_CPP 이거나 EAction·Envelope 처럼 여러 파일을 대조해야 하는 변경이면 `effort: "medium"` 이상으로 올린다
     (파일 간 누락은 빌드·테스트가 못 잡아 리뷰어가 유일한 관문). Dev 호출에는 effort 를 지정하지 않는다.
     Dev 와 컨텍스트를 공유하지 않는 별개 호출이어야 한다. 프롬프트에 작업공간 경로·TaskSpecification·
     `git diff` 확인 지시를 넣고, 첫 줄 PASSED/WARNING/REJECTED 한 단어 + 둘째 줄부터 `파일:줄 — 내용`.
     기준은 AGENTS.md 3대 원칙 + `ue5_cpp.md`/`python_backend.md`(널 가드, BB 단일 진입점, EAction 4곳, Envelope 3곳).
     WARNING = 동작은 맞으나 규칙·품질 문제 → Dev 에게 경고만 고치게 하고(동작 유지) 검증으로 진행.
     REJECTED = 버그·계약 위반 → 빌드를 건너뛰고 Dev 재작업.
     응답 첫 줄이 셋 중 하나가 아니면 반려가 아니라 리뷰 실패 — 재호출하고 Dev 를 반려하지 않는다.
   - 검증: `python <ws>\tools\sol_pi.py verify <all|python>` — UE5_CPP·ASSET_3D 는 `all`, PYTHON_BACKEND 는 `python`.
     **워크트리 안의 sol_pi.py 를 돌려야** 그 트리가 검증 대상이 된다. 장시간이므로 백그라운드 + Monitor.
6. **재작업** — QA 실패 시 에러 꼬리 12줄을 반려 사유로 Dev 재호출. **최대 2회**. 초과하면 멈추고
   사람에게 에스컬레이션(작업공간 경로·마지막 에러 보고).
7. **Master 승인** — `git diff` 와 QA 결과를 직접 읽고 목표 충족 여부를 APPROVED/REJECTED 로 판정(근거 1~2줄).
   REJECTED 면 사유를 Dev 로 돌려 재작업(횟수 포함).
8. **마무리** — 승인되면 워크트리에서 커밋하고(`feat: <goal 50자>`, 커밋 규칙은 `.agents/rules/git.md`),
   **SPEC 에 마일스톤(M1·M2…)이 있으면 마지막 M 까지 2~7 단계를 반복한다**(M 사이에 사용자 확인 불필요, 결과만 보고).
   마일스톤마다 커밋하되 워크트리는 남긴다. 다음 M 의 미결 값은 SPEC 초안값으로 진행하고 보고에 적는다.
   **마지막 M 이 승인되면** 현재 폴더(메인 트리)의 현재 브랜치로 통합한 뒤 워크트리를 지운다(묻지 않음):
   - 메인 트리에서 **마일스톤 커밋 전체를 한 번에** `git cherry-pick <첫커밋>^..<마지막커밋>` — 중간 M 마다 따로 체리픽하지 않는다.
     메인 트리가 dirty 해도 겹치는 파일이 없으면 된다. 겹치면 멈추고 보고. 통합 뒤 메인 트리에서 `verify all` 한 번.
   - 워크트리 잔여물이 생성물뿐인지 `git status` 로 확인(`uv.lock`·`.ignore`·`Content/Maps/` UAT umap 등) →
     `git worktree remove --force <ws>` · `git branch -D <브랜치>`(cherry-pick 이라 -d 로는 안 지워짐).
     생성물이 아닌 변경이 남아 있으면 지우지 말고 보고.
   - `docs/memo.md` Done 한 줄을 **묻지 말고 바로 반영**한다(해소된 Todo 정리 포함). 헤드셋 확인 필요분은 `docs/DoList.md` 에.

### 함정

- UAT 가 `NewProjectTest.umap` 을 저장해 `git add -A` 에 섞인다 → 커밋 전 `git status` 로 `Content/Maps/` 가 끼었는지 확인해 제외.
- 워크트리 `verify all` 의 Python 16 error 는 환경 문제(pitfalls U-6) — 워크트리 `OmniAgent_VR_System/CognitiveEngine` 에서
  메인 트리 `.venv/Scripts/python.exe -m pytest tests -q` 로 재확인한다(`-p no:logging` 붙이면 caplog 테스트가 error 로 거짓 실패).
- 워크트리 에디터를 MCP 로 다루려면 `Saved/Config/WindowsEditor/RemoteControl.ini`(ignore 대상, 원격 Python 허용)를
  메인 트리에서 복사한 뒤 에디터를 띄운다. 없으면 `Default__PythonScriptLibrary cannot be accessed remotely`.
  MCP 는 포트 30010 고정이라 에디터는 한 번에 하나만 띄운다.
- 워크트리 코드로 서버를 띄우려면 `OmniAgent_VR_System/CognitiveEngine/.env`(ignore 대상, `WS_AUTH_TOKEN`)도 복사한다.
  없으면 모든 메시지가 `[Auth] WS_AUTH_TOKEN 미설정` 으로 거부된다. 호감도 DB `app/data/affinity.db`(ignore 대상)도 없으면
  새로 만들어져 전원 0 — 파티 합류가 게이트에서 전부 거절된다. 메인 트리 것을 복사. 서버를 다시 띄우면 UE 는 재연결 5회 후 오프라인 → PIE 재시작.
- `sol_pi verify` 는 **어떤 UnrealEditor 든 떠 있으면 빌드를 거부**한다(워크트리여도). 저장 안 된 패키지 없음 확인 →
  MCP `quit_editor` → 검증 → 끝나면 메인 에디터 재기동. 빌드는 한 번에 하나씩(여러 Dev 가 동시에 끝나도 verify 는 순차).
- Agent 호출은 항상 `model: "sonnet"`. 부모 모델 상속 금지(CLAUDE.md).
- 사용자 변경이 있는 현재 트리에서는 돌리지 않는다. 다른 에이전트(Codex/Gemini)가 같은 트리를 편집할 수 있다.
