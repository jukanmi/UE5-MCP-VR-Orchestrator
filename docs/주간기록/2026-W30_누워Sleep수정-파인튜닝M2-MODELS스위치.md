# W30 (2026-07-20 ~ 07-26) — "누워"→Sleep 3-track 수정 · 파인튜닝 M2 완주 · MODELS 스위치

<!-- Memo.md Done(07-20·07-21·07-23) 기반 소급 작성(2026-08-29). 커밋은 07-20 PR #21 머지 1건 — 나머지는 데이터·모델 산출물이라 커밋 해시가 아닌 산출 에셋으로 남았다. -->

## 핵심

한 가지 증상("누워"라고 해도 NPC 가 눕지 않음)을 **3개 층위에서 동시에** 고친 주. 진단은 `train_logger` 로그로 시작했다 — LLM 이 "침대에 누워"에 Sit/Comfort/Move 만 내고 **Sleep 을 0회** 낸다는 것을 실측으로 확정했다. 여기서 중요한 부수 소득이 있었는데, 처음엔 `train_logger` 자체가 한글을 깨뜨리는 줄 알았으나 **콘솔 렌더 착시**였고 파일은 utf-8 정상이었다(이후 판정은 코드포인트로 하기로 확립).

원인이 프롬프트·C++·학습데이터 셋에 나뉘어 있어 세 곳을 다 고쳤다. 특히 **학습데이터 편향**이 근본이었다 — 데이터 생성기가 `sleep/rest/inn/bed` 키워드를 전부 Sit 으로 뭉개 **Sit 233 : Sleep 0** 이라는 극단 편향을 만들고 있었다. 분리 후 재실행하니 Sit 193 : Sleep 252 로 역전됐다.

주 후반은 파인튜닝 M2(Stage1 e4b) 완주 — LoRA 학습 → GGUF → Ollama 등록 → 광역 eval → `MODELS` 스위치까지. **gold 액션 재현 3/8 → 8/8** 로 뛰었다.

## 주요 작업

### "누워"→Sleep 미발화 3-track 수정 (07-20)

- **few-shot** (`prompts.py`) — Sleep 이 곁다리 괄호로만 있던 것을 독립 정식 예시로 승격(`lie on a bed / "누워" → Sleep target=bed_id`). 더불어 "명령(앉아/누워/따라와)은 NPC **자신**이 수행 — 청자에게 권하는 대사만 내지 말 것" 규칙 추가. Moca 의 ASMR 페르소나가 "플레이어를 눕히는" 방향으로 새던 것을 차단.
- **C++ 가드** (`NPCActionComponent.cpp`) — 가구行 자세 몽타주를 LLM action type 이 아니라 **가구 타입**이 결정하도록 변경. Bed→LieDown, Seat→SitDown. LLM 이 침대에 Sit 을 내도 눕게 교정된다. `bIsSit`/`bIsLie` 도 MediaKey 기준으로 정합(L548). **빌드 검증 완료, PIE 미검증.**
- **학습데이터 편향** (`build_dataset_from_raw_lorebooks.py`) — `sleep/rest/inn/bed` 계열을 Sleep+Bed 로 분리(situation `nearby_furniture` 도 `furn_type` 변수화). `npc_info.parquet` 재실행 → **Sit 233:Sleep 0 → Sit 193:Sleep 252**. `lorebook_raw_scenarios.yaml` 재생성.

> 반영 시점이 층마다 다르다 — few-shot 은 서버 재시작 즉효, C++ 은 빌드 후, 데이터는 다음 학습 사이클.

### 파인튜닝 M2 (Stage1 e4b) 완주 (07-21)

- LoRA r32 · 721행 · 543스텝 학습 → GGUF Q4_K_M → Ollama `gemma4-e4b-dialogue-v1` 등록. 상세는 `finetune/RESULT.md`.
- **환경 확정**: chatbot/venv 재사용(unsloth 2026.7.3, **WSL 불필요**) · 베이스 `unsloth/gemma-4-E4B-it`.
- **Blackwell 크래시 회피 4종**: `TORCHDYNAMO_DISABLE` · `_save` 패치 · seq1024 · gemma4 `<|turn>` 마커.
- eval: 학습 프롬프트에서 `{"type":"Sleep","target":"Bed_02"}` gold 완벽 재현 — 다만 **overfit 경향**이라 판단해 이 시점엔 `MODELS` 스위치 보류.

### MODELS 스위치 + 광역 eval (07-23)

- `llm_factory.py MODELS["gemma4_slm"] = "gemma4-e4b-dialogue-v1"` 적용.
- **광역 eval** (`finetune/eval/broad_result.json`):
  - gold 액션 재현 **3/8 → 8/8** — 구모델의 Move→Wait 오답, Comfort/Trade 누락, Investigate→Move 오답이 전부 해소.
  - held-out 신규 발화 의도인식 **0/6 → 2/6**(Sing·Sleep).
  - speech 는 매번 다름 → overfit 이 아니라 **실제 선택 능력 개선**으로 확인.
- 서버 재시작 후 `/api/debug/prompt` 스모크 — `ActionType:"Sleep"` 정상. grammar 강제 덕에 broad_eval 에서 보이던 raw-string 포맷 깨짐은 재현되지 않았다.
- **한계**: `target="Player"` 가 나오는 건 debug 엔드포인트가 `nearby_furniture` 를 주입하지 않는 구조적 문제(모델 문제 아님) → 실가구 타겟팅은 PIE 필요.

## 메모

- **train_logger 무죄** — 콘솔에서 한글이 `` 로 보이는 건 파일 손상이 아니라 콘솔 렌더 착시다. 판정은 반드시 코드포인트로 할 것.
- **3-track 수정의 교훈** — 프롬프트만 고치면 데이터가 되돌리고, 데이터만 고치면 다음 학습까지 기다려야 한다. LLM 행동 버그는 프롬프트·코드 가드·학습데이터 세 층을 같이 봐야 한다.

## 커밋

| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 07-20 | `97582ae` | Merge pull request #21 from jukanmi/feature/furniture |

> 이 주의 산출물 대부분은 커밋이 아니라 모델·데이터 에셋(`gemma4-e4b-dialogue-v1`, 재생성 시나리오 yaml)으로 남았다.

## 사용자 작업 보충 (DoList Done 이관, 2026-09-13)

- **자세 유지 PIE — 앉기 계열**(2-5, 날짜 미기재) — "앉아" SitDown 후 Sit_Idle 유지 · "일어나" SitUp 복귀 · 이동 명령 시 자세 해제. "누워" 는 이 시점 미검증(활성 목록 잔류).
