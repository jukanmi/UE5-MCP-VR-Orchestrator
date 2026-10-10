# 제안: NPC LLM · Jev 개선 방안 (조사 보고, 코드 변경 없음)

> 작성 2026-10-08, 브랜치 `feature/npc-llm-jev-improve`(워크트리), 읽기 전용 조사. 구현·재학습·서버 기동은 하지 않았다.
> 근거 표기: `파일:줄` = 이 저장소 코드, `로그` = 메인 트리 `OmniAgent_VR_System/CognitiveEngine/app/data/train_logs/*.jsonl`(`train_logger` 가 남기는 Stage1/Stage2 프롬프트·응답·소요시간. gitignore 라 워크트리에는 없음), `memo` = `docs/memo.md`(메인 트리 작업본 — 새 Todo "파인튜닝 SLM 이 JoinParty·target_poi 를 내지 않음"은 워크트리 HEAD 에는 아직 없다).
> **추정**은 직접 확인하지 못한 것. PIE 를 돌리거나 `ollama ps`/`nvidia-smi` 를 보지 않았으므로 VRAM·지연 수치는 로그와 코드에서 읽은 범위에 한한다.

---

## 0. 한눈에

| # | 문제 | 핵심 근거 | 1순위 처방 | 재학습 |
| --- | --- | --- | --- | --- |
| P1 | SLM 이 `JoinParty`·`LeaveParty`·`poi` 를 못 낸다. `Stop` 은 의미가 바뀌어 위험 | 학습 의도표에서 "같이 가자"가 `Follow`(`intents/common.yaml:35,55`), "멈춰"가 `Stop`(`:134`). 로그 10-08: 합류 요청에 `Follow` | 서버 의도 사전 힌트 + 사후 보정 | 불필요 |
| P2 | 재학습 도구가 서빙 프롬프트와 어긋나 지금은 못 돌린다 | 생성기 2개가 `story_goal` 없이 `format()` 호출 | 생성기 단일 소스화 | 선행 조건 |
| P3 | 지연: 유휴 뒤 첫 턴 Stage1 7.6~10.2s + Stage2 2.7~4.5s | 로그 10-05·10-08 | e4b `keep_alive` 연장, Stage2 처리 결정 | 불필요 |
| P4 | VRAM 압박 (e4b 5.3GB + qwen3:8b 5.2GB + Jev CUDA + PIE) | `RESULT.md`, `llm_factory.py:114`, DoList 3 | Stage2 제거/축소가 가장 큼 | 불필요 |
| P5 | 대사가 질문과 안 맞고 직전 문장을 복사한다 | 로그 10-08: 응답이 history 첫 줄과 글자까지 같다 | 기록 주입 정리 + RAG 정리 + 반복 감지 | 불필요(근본은 v4) |
| P6 | 프롬프트 꼬리가 쌓인다 | 시스템 3.9k→4.4k자, 사용자 컨텍스트에 POI·plan 문장 | 조건부 주입 | 불필요 |
| P7 | Jev 가 휴리스틱으로만 돈다 | 체크포인트 gitignore, 전투는 설계상 휴리스틱 | 체크포인트 배포 경로 확정 | 불필요 |
| P8 | Jev 잔여 빈틈(첫 요청 지연, 관측 부재, 실데이터 로깅 없음) | `main.py`, `jev_service.py:224` | 기동 시 워밍 + 로드 상태 노출 | 불필요 |

---

## 1. 현재 구조 요약

### 1.1 대화 (프롬프트 한 번의 흐름)

| 단계 | 내용 | 위치 |
| --- | --- | --- |
| 입력 | UE `SendPlayerDialogue` → `prompt` Envelope. `valid_targets`(NPC·가구·바닥 아이템), `nearby_furniture`, `npc_inventory`, `known_pois` 동봉 | `NPCManager.cpp:300-410` |
| 컨텍스트 조립 | 인젝션 정규식 → `natural_context`(발화·HP·인벤·가구·POI·plan 한 줄) | `interface_input.py:261-289` |
| Stage1 | **`gemma4-e4b-dialogue-v1`**(LoRA, 721행 학습, GGUF 5.3GB). Ollama `format=` 스키마 강제(`DialogueResponse`, target enum 주입), temp 0.5, `num_predict` 300, `num_ctx` 4096. NPC 별 `asyncio.gather` | `llm_factory.py:68,91,118` · `dialogue.py:326-384` |
| Stage1 시스템 프롬프트 | 고정 본문(학습 분포) + persona + RAG 3청크 + 기록 5개 + (꼬리) 파티 어휘·현재 일행 | `prompts.py:10-60` · `dialogue.py:331-333` |
| 사후 처리 | 파티 게이트(호감도 20·정원 4, 거절 대사 교체) → 필드 매핑 → Rules(어휘·좌표·필수 파라미터 검증) | `dialogue.py:282-323` · `interface_output.py` · `rules.py:168-228` |
| Stage2 | replan 턴에만. **`qwen3:8b`**(비파인튜닝, `keep_alive` 30s)가 Stage1 발화를 보고 `goal`/`steps` 산출. 소비되는 건 `goal` 한 줄뿐(`steps` 는 주입 금지) | `dialogue.py:432-507` · `interface_input.py:165-187` |
| replan 조건 | plan 무·전투 최초 전환·plan 달성·`MaxTurnsPerPlan=4`. `plan_achieved` 는 사실상 항상 false(아래 P5) | `NPCStateComponent.h:229-238` |
| 스토리 | 디렉터(`gemma4:cloud` 31B)가 비트 전이 때만 호출. goal 이 Stage1 `Story objective` 줄·Stage2 입력으로 | `story/director.py` · `dialogue.py:207-210` |

현재 구조 메모: `IMPORTANCE_MODELS`(core=12B)는 디버그 표시용일 뿐 Stage1 은 항상 `STAGE1_MODEL`(`llm_factory.py:126`, `model_for_importance` 호출처는 `debug_routes.py` 두 곳뿐).

### 1.2 Jev

| 구분 | 흐름 | 체크포인트 |
| --- | --- | --- |
| daily(비전투 일상) | UE 컨트롤러가 비전투·큐 빔 `JevDailyIdleSeconds`(10s)·LLM 직후 15s 경과 시 `jev_query(domain=daily)` 송신(`SmartNPCAIController.cpp:720-769`) → `NPCActionComponent::BuildJevDailyQuery` 가 풀(actors·places·pois·items·ground·media)·metrics 조립(`:2907`) → 서버 `run_daily_passes`: 활동 1패스 + 슬롯 순차 최대 4패스(`jev_service.py:475-519`) → 워치독 0.3s(`SmartNPCAIController.h:199`) | `app/models/jevlike_tactics.pt`(0.5MB). **없으면** `_load` 가 `[Jev] 체크포인트 없음 → 휴리스틱 폴백`(`jev_service.py:121`)을 찍고 손튜닝 로짓(`:388-467`)으로 동일 형식 응답 |
| combat(승수) | 같은 경로, 5~20ms | **체크포인트가 있어도 쓰지 않는다** — 전투는 라벨 피팅 선형 휴리스틱 + 성격 돌파(`jev_service.py:165-168`, 2026-09-24 결정, 포위 시 aggressive 0% 노이즈 회피) |
| 파티원 | daily 요청을 안 보내고 1초마다 추적 복구(`SmartNPCAIController.cpp:735-745`) | — |

---

## 2. 문제·빈틈

### P1. 학습 분포 밖 어휘 — JoinParty / LeaveParty / poi / Stop

로그 10-08(실제 PIE 한 턴): 발화 `Guard, 우리 일행에 합류해서 같이 가자` → 응답 `{"speech":"그래, 아직도 남아있어.","actions":[{"type":"Follow","target":"Player"}]}`. 프롬프트 꼬리에 `Party actions: JoinParty = …` 가 있었는데도 `Follow`.

| 근거 | 위치 | 의미 |
| --- | --- | --- |
| 학습 의도표에서 "같이 가자"·"나랑 같이 움직여"는 `Follow` 정답 | `finetune/data/synth/intents/common.yaml:35-56` | 파인튜닝이 굳힌 매핑을 꼬리 한 줄이 못 이긴다 (`FINETUNE_v4_requirements.md` §1 "프롬프트로 메워지지 않는 것" 과 같은 현상) |
| 고정 본문의 "MANDATORY" 예시가 `"follow me" → Follow`, 액션 목록에 JoinParty·LeaveParty 없음 | `prompts.py:22-23,37-39` | 새 어휘는 본문 밖 꼬리(`:56-60`)에만 있다 |
| 학습 데이터·생성기 어디에도 `JoinParty`·`LeaveParty`·`poi` 없음 | `finetune/` grep 0건 | 스키마(`actions.py:58-63,106`)상 낼 수는 있으나 가르친 적이 없다 |
| **`Stop` 충돌** — 학습은 "멈춰·그만해·스톱"(14문장) → `Stop`. 지금 UE 는 `Stop` = 파티 해산(`NPCActionComponent.cpp:539`) | `intents/common.yaml:134-` · 꼬리가 "평범한 멈춰는 Idle" 이라 지시 | 파티원에게 "멈춰" 하면 해산될 수 있다(추정: 꼬리 지시가 학습을 이긴다는 증거 없음). `_gate_party_actions` 는 `Stop` 을 보지 않는다(`dialogue.py:287`) |
| `poi` 는 "우물로 가"(명시 이동 명령)에서 통했다 | 커밋 `8371bd4e`(PIE Guard 가 Well 177cm 정지) | POI 안내가 **사용자 메시지**(`interface_input.py:255-258`)에 있고 의도가 명확해서로 보임(추정). 간접 표현("호수 가봤어?")·`Move` 외 문맥은 미검증 |

판정: **JoinParty 는 프롬프트 수정만으로는 안 나온다.** `target_poi` 는 명령형에선 동작하나 일반화는 미검증.

### P2. 재학습 도구가 서빙 프롬프트와 어긋남

- `finetune/data/synth/generate_stage1.py:97-108`, `generate_expanded_stage1.py:206-218` 은 `DIALOGUE_STRUCTURED_PROMPT.format(...)` 에 `story_goal` 을 넘기지 않는다. 프롬프트는 2026-09-18 에 `{story_goal}` 이 추가됐다(`prompts.py:50`). `str.format` 의 `KeyError` 로 **두 스크립트 모두 지금은 실패한다**(코드상 확실, 실행은 안 함). 서빙 쪽은 `tests/test_party_update.py:63` 이 `story_goal` 을 넣어 통과 중이라 드러나지 않았다.
- 학습 행은 `rag_context="None"`, `memory="None"`, 기록은 거의 빈 값(`generate_stage1.py:102-108`)이다. 서빙은 RAG 600~1,250자 + 기록 5개(이벤트 포함)가 늘 들어간다 → **학습/서빙 분포 차**. v1 의 "백지 기록에선 7/8 통과, 오염 기록에선 흔들림"(`FINETUNE_v4_requirements.md` §1)과 일치.
- 서빙 꼬리(`PARTY_VOCAB_PROMPT`)는 생성기가 쓰는 `DIALOGUE_STRUCTURED_PROMPT` 밖이라 학습에 포함될 수 없다.
- 현재 `finetune/data/processed/` 에는 v4 소규모 파일만 있다(`v4_master` 42행·`curated` 58·`harvested` 58, 모두 JoinParty 없음). v1 학습 원본(721행)은 없고 스크립트로 재생성해야 한다.
- v4 요구사항 문서(15개 항목)는 있으나 착수되지 않았다. `plan_achieved` 는 로그 기준 **46건 중 true 0건**(2026-09-18 이후 Stage1 로그 집계) — `MaxTurnsPerPlan=4` 가 유일한 plan 종료 경로다.
- 회귀 하네스 `tools/finetune_eval/stage1_bench.py` 는 파티·POI 케이스가 없고, `DIALOGUE_STRUCTURED_PROMPT.format(...)` 만 써서 운영 꼬리를 반영하지 않는다(`:124`).

### P3. 지연

로그 10-05·10-08 (Stage1 `gemma4-e4b-dialogue-v1`, Stage2 `qwen3:8b`):

| 상황 | Stage1 | Stage2 | 비고 |
| --- | --- | --- | --- |
| 유휴 5분 초과 뒤 첫 턴 (13:12, 13:35, 13:43, 13:50, 10-08) | 7.6 / 9.4 / 10.2 / 10.1 / 7.6s | 3.4 / 4.5 / 4.3 / 4.1 / 2.7s | Stage1 뒤에 Stage2 가 **직렬** → 체감 11~15s |
| 같은 NPC 후속 턴 | 1.5~3.5s (중앙 약 2.5s) | 없음(replan 아님) | |
| 다른 NPC 첫 말걸기, 5분 안 | 5.4s | **10.1s** | 시스템 프롬프트 접두가 달라 KV 재사용 불가 + qwen3 재로드/경합(추정) |

- 원인 후보: e4b `keep_alive="5m"`(`llm_factory.py:133`) 만료 → 재로드, 플래너 `30s` 만료 → 매 replan 재로드. 기동 시 프리웜은 e4b 에 한 번뿐(`main.py:80-94`).
- UE 쪽에 LLM 대기 중 즉출하는 bark·생각 제스처는 없다(`bark`/`thinking` grep 결과 해당 코드 없음). 대화 시작 시 플레이어를 바라보는 `SetDialoguePartner` 뿐(`NPCManager.cpp:301`).
- 서버가 `speech` 가 JSON 맨 앞에서 먼저 생성되는데도 스트리밍하지 않고 전체 완료 후 한 번에 응답한다(`llm_factory.py:211`, `"stream": False`).

### P4. VRAM 압박

- 상주 후보: e4b-v1 GGUF 5.3GB(`RESULT.md`) + qwen3:8b 5.2GB(`llm_factory.py:114`) + Jev 의 torch CUDA 컨텍스트(`select_device("auto")`, `jev_service.py:126`; 크기는 **추정** 수백 MB) + PIE/VR + (ComfyUI 등). 16GB 포화는 OOM 이 아니라 조용한 감속(`pitfalls.md` E, 메모리 기록 "VRAM 스필오버 진단").
- DoList 3("12B+e4b+PIE 동시 부하 Ollama 500/ReadTimeout")은 12B 가 Stage1 에 안 쓰이는 지금(위 1.1)은 stale 일 수 있다 — 현재 동시 상주는 e4b+qwen3:8b+Jev.
- KV 양자화 금지(크래시, `pitfalls.md` A). `num_ctx` 4096 은 2048 에서 잘림 사고 뒤 올린 값(`llm_factory.py:65-68`)이라 내릴 때 프롬프트 길이 확인 필수.

### P5. 대사 품질 — "합류해서 같이 가자" → "그래, 아직도 남아있어."

로그 10-05·10-08 에서 이 대사의 계보를 거슬러 올라갔다.

1. 10-05 13:35 Guard 첫 턴 `hi`: Story objective 는 `None`. RAG 가 persona.md·lore("폐허가 된 성문 안쪽 피난처…")를 그대로 넣었고 응답은 `"안녕. 성문은 아직도 이곳에 남아있나?"` — 로어 문장을 **질문으로 되받음**.
2. Stage2 는 Stage1 발화만 입력으로 받아 `goal="성문의 상태를 확인하고 플레이어에게 알리기"` 를 만들었고, 이후 턴 사용자 컨텍스트에 `Background goal from earlier: …` 로 되먹임됐다(Stage1→Stage2→Stage1 **피드백 루프**).
3. `maybe?` → `"아직도 남아있나? (확인)"`(+`Read` 액션), `yes` → `"그래, 아직도 남아있어."`.
4. 10-08 합류 요청의 history 첫 줄이 정확히 `Guard: 그래, 아직도 남아있어.` — 응답이 **그 줄을 글자까지 복사**했다. 질문(합류 제안)은 무시됨. `memo` 의 "대화 기록이 대사를 지배한다"와 동일 증상.

기여 요인:

| 요인 | 근거 |
| --- | --- |
| 기록 5개는 **5개 항목**이지 5턴이 아님(턴 1 = Player+NPC 2항목). 반사·융합·승리 `Event` 도 같은 목록에 들어와 기록이 이벤트로 채워진다 | `memory_manager.py:264-266`, `dialogue.py:228`, `main.py:357`. 로그 10-05 James·Guard·Elara 첫 턴은 기록이 **전부(Elara 는 4개 전부) `Event: …반사적으로 …`**. `python_backend.md` §3 의 "최근 5턴"과 실제 불일치 |
| RAG 는 영어 임베딩 `all-MiniLM-L6-v2` 로 한국어 질의를 검색, 점수 임계값·카테고리 필터 없이 항상 top-3 | `rag_utils.py:38-39,143`. persona.md 청크는 persona yaml(말투·성격)과 **중복 주입**. 검색 품질은 추정(모델이 영어 중심) |
| 엉뚱한 액션 방출: 로그 10-05 이후 응답 46건 중 비의도 액션 7건(`SignalAllies`×2·`Investigate`×2("안녕"에 `loc 100,100,0`)·`Dodge`·`Read`·`Comfort`("팀에 들어와"에 위로)) | 2026-09-18 이후 Stage1 로그 집계. 모델이 문맥 없는 `Event` 줄을 따라가는 것으로 추정 |
| `temperature=0.5` 고정, 반복 감지 없음 | `dialogue.py:351` |
| Stage1 이 기록 오염에 약함은 v4 문서에 이미 실측됨 | `FINETUNE_v4_requirements.md` §3 (k=0 이면 문형 복사 소멸) |

### P6. 프롬프트 토큰 증가

시스템 프롬프트 평균(로그): 3.9k자(09-04) → 4.4k자(10-05, 10-08). 본문 2,906자는 학습 분포라 고정이다. 덧붙은 것:

| 항목 | 크기 | 위치 | 매 턴? |
| --- | --- | --- | --- |
| `Story objective` 줄 | +79자 + goal | `prompts.py:50` | 예 |
| 파티 어휘 꼬리 | 268자 | `prompts.py:56-60` | 예(파티 무관하게) |
| 현재 일행 줄 | ~18자 + 멤버 | `prompts.py:63` | 일행 있을 때 |
| RAG 3청크 | 600~1,245자 | `dialogue.py:224` | 예(발화 무관) |
| 기록 | 110~430자 | `dialogue.py:228` | 예 |
| POI 안내(사용자 컨텍스트) | 헤더 130자 + 최대 8줄 + `Nearby place` ≤3줄 | `interface_input.py:233-258` | known_pois 있을 때 항상 |
| plan 래퍼 | 182자 + goal | `interface_input.py:185-187` | plan 있을 때 |
| 가구·바닥 아이템 | 가구당 ~50자 | `interface_input.py:190-222` | 해당 시 |

- 꼬리는 `Conversation history:` 블록 **바로 뒤에 붙어** 로그에서 기록의 일부처럼 보인다(로그 10-08 `…SignalAllies을(를) 실행했다. | Party actions: …`). 모델이 지시와 기록을 구분 못 할 가능성(추정).
- `valid_targets` 문자열에 적(`DemonLord`, `Commander_Vorg`)·자기 자신(`Guard` 프롬프트의 `Guard`)이 NPC 이름으로 들어간다(로그 10-05). 바닥 아이템 instance id 도 같은 목록에 합류한다(`NPCManager.cpp:359-368`, 로그에선 아이템 없는 상황이라 미확인).
- 토큰 환산은 추정: 한글 혼합 4.4k자 ≈ 1.5~2.2k 토큰. 2026-09-18 에 2011 토큰으로 2048 을 넘겨 잘렸던 전례(`llm_factory.py:65-67`)가 있어 여유 관리 필요.

### P7. Jev 가 휴리스틱으로만 도는 이유 (두 갈래를 구분해야 한다)

| 갈래 | 사실 | 결과 |
| --- | --- | --- |
| 체크포인트 부재 | `jevlike_tactics.pt` 는 메인 트리 `app/models/` 에만 있다. `.gitignore:60 **/models/*` 로 추적 안 됨 → **워크트리·다른 PC 는 항상 폴백**. 경로는 `JEV_CHECKPOINT` 환경변수로 덮어쓸 수 있다(`jev_service.py:31-33`). `jevlike` 패키지 자체도 git URL 설치(`requirements.txt:28`) | daily 정확도가 모델 77.4% → 휴리스틱 48.4%(활동), 슬롯 69.6% → 51.9%(`SPEC_jev_daily.md` M2 기준 17~19) |
| 전투는 설계상 휴리스틱 | 체크포인트가 있어도 안 쓴다(`jev_service.py:165-168`) | 로그 문구와 무관, 버그 아님 |

- 학습 원본(`combos_daily*.jsonl`, `combos_combat.jsonl`, 골드셋)은 git 추적, 파생물(`*.pt`, `soft/`)은 ignore(`finetune/jev/data/.gitignore`) — 재현은 가능하나 절차가 문서화돼 있지 않다(`sweep.py` 가 build→train 을 묶음).
- 모델 평가 정답은 **라벨러 LLM(gemma4:26b) 등급**이다. 사람 골드(50건) 미채점이고 편중(look_at 24·stay 15/50)이다(`memo` Jevlike 잔여 ①④).

### P8. Jev 잔여 빈틈

| 문제 | 근거 | 영향 |
| --- | --- | --- |
| 기동 시 워밍 없음 — 첫 `jev_query` 가 `get_jev_service()` 로 `jevlike`(torch) import·체크포인트 로드를 **동기 핸들러 안에서** 수행 | `main.py:738-775`, `jev_service.py:224-229`, lifespan 에 호출 없음(`main.py:100-108`) | 첫 요청은 0.3s 워치독 초과로 폐기될 가능성, 로드 시간 동안 이벤트 루프 정지(소요 시간은 추정) |
| 모델/휴리스틱 구동 상태를 볼 수단이 로그 한 줄뿐 | `debug_routes.py` 에 Jev 항목 없음 | 조용히 폴백돼도 모름 |
| 실플레이 결정 로깅 없음 | `train_logger` 는 LLM·Rules 만 기록 | 합성 상황(라벨러 LLM)만으로 학습, 실분포 점검 불가 |
| 학습 POI 이름이 운영과 다름 | 학습 `POI_NAMES`(Gate·Plaza·Well·Market·Shrine·Dock·Tower·Forge, `jev_dataset.py:57`) vs 레벨 POI 11개(Hideout·Ruins·Library·Forest·Bridge …). 풀은 반경 15m 안만(`NPCManager.cpp:466-476`) | 바이트 인코더가 낯선 id 를 만남(영향 크기는 추정, 거리 항이 지배할 가능성) |
| `goal` 컨텍스트에 Stage2 goal 이 들어감 | `BuildJevDailyQuery` `goal`(`NPCActionComponent.cpp:~2925`) | P5 의 오염된 goal 이 Jev 입력에도 번짐 |
| 파티 합류 요청 전송 후 늦은 응답 | 이미 처리됨(`SmartNPCAIController.cpp:663-668`) | 문제 없음 |

---

## 3. 개선 방안

표기: 비용은 작업량(사람·일 추정), VRAM 은 상주 증감, 위험은 회귀·오동작 가능성.

### 3.1 P1 — 합류·해산·이동 어휘

| 안 | 내용 | 효과 | 비용 | 재학습 | VRAM | 위험 | 의존 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **A. 서버 의도 매핑 (권장)** | ① 사전 힌트: `interface_input` 이 초대 키워드(합류·일행·파티·동료·팀·같이 다니…) 감지 시 `natural_context` 에 한 문장 추가("플레이어가 일행 합류를 청한다. 받아들이면 JoinParty, 거절이면 대사만") — POI 안내가 사용자 메시지에서 통한 선례. ② 사후 보정: `_gate_party_actions` 앞에서 `Follow(Player)` + 초대 키워드면 `JoinParty` 로 치환(게이트가 호감도로 거른다). 해산 키워드는 `LeaveParty`. ③ **파티원이 낸 `Stop` 은 해산 키워드 없으면 `Idle` 로 치환** | 즉시 동작, 간접 표현 한계는 키워드가 정함 | 0.5일: `dialogue.py`·`interface_input.py`, `tests/test_party_update.py` 케이스 추가 | 없음 | 0 | 키워드 오탐/미탐. ①이 없으면 ②만으로는 대사가 수락 톤이 아닐 수 있음(로그 10-08 대사가 무관) | "같이 가자" 단독을 합류로 볼지 결정(§5) |
| B. 동적 grammar | 초대 의도 감지 시 `schema_override`(이미 `_dialogue_schema_with_targets` 패턴, `dialogue.py:57-80`)로 `actions[].type` enum 을 좁혀 `JoinParty`/`Follow` 만 허용 | 액션 결정론 | A + 0.5일 | 없음 | 0 | `speech` 가 `actions` 보다 먼저 생성돼 대사-액션 불일치. A① 필수 | A |
| C. 학습 보강 + 재학습 (v4) | `intents/party.yaml`(JoinParty·LeaveParty·Stop 분리, 거절 대사 샘플), `poi` 이동 샘플, "같이 가자" 재라벨, 파티 꼬리를 본문에 편입 | 간접 표현까지 일반화(추정), 꼬리 제거로 토큰도 절감 | 2~4일(추정): 생성기 수리(P2)·teacher(gemma4-12b) 라벨·학습 1~3h·GGUF 변환·bench | **필요**(e4b unsloth 경로는 동작, 12B 블로커는 Stage2 용이라 무관 — `RESULT.md`) | 학습 중 Ollama·UE 종료 필요(VRAM 16GB), 서빙 불변 | v2/v3 의 "줘→HandObject" 회귀 전례. seq 1024 캡. 어휘를 또 추가할 때마다 재학습 | P2, bench 케이스, 사용자 결정 |
| D. Jev command 트리거 | `SPEC_jev_daily.md` 부록 B: 발화+풀로 jevlike 가 행동 선택, LLM `actions` 는 Trade 만 | 어휘 확장 시 재학습이 가볍다(0.5MB·분 단위) | 1주+ (학습 데이터 ≥3,000 합성, C++ 조립/선점, LLM 스키마 변경) | jevlike 학습 | 0 | 큰 구조 변경, 거절·대사 연동 설계 필요 | 장기. 지금은 비권장 |

### 3.2 P2 — 재학습 도구 정상화 (C 의 선행 조건, 단독으로도 가치)

| 안 | 내용 | 비용 | 위험 |
| --- | --- | --- | --- |
| A. 최소 수리 | 두 생성기에 `story_goal="None"` 추가. 생성기 스모크를 pytest 에 포함(서빙 프롬프트 변경 시 즉시 깨지게) | 0.1~0.2일 | 낮음 |
| **B. 서빙 함수 단일 소스화 (권장)** | 생성기가 프롬프트를 직접 `format` 하지 않고 서빙의 조립 함수(꼬리 포함)를 호출. 학습 행에 RAG·이벤트 포함 기록 비율 도입 | 1일 | 학습 분포 변화 → 재학습·bench 동반 |
| C. bench 확장 | `stage1_bench.py` 에 파티(합류·거절·해산·"멈춰")·POI(명령/간접)·기록 오염(`--seed-history`) 케이스 추가, 운영 꼬리 포함 | 0.3일 | 낮음. A·C 를 먼저 해야 어떤 방안이든 효과를 숫자로 비교 가능 |

### 3.3 P3 — 지연

| 안 | 내용 | 효과(추정) | 비용 | VRAM | 위험 |
| --- | --- | --- | --- | --- | --- |
| **A. e4b `keep_alive` 연장 + 주기 핑 (권장)** | `llm_factory.py:133` 5m → 30m(또는 -1), WS 연결 중 4분마다 빈 로드콜(`_prewarm_core_llm` 방식) | 유휴 뒤 첫 턴 Stage1 7.6~10.2s → 약 2.5~3.5s | 0.1일 | e4b 5.3GB 상주 시간 증가(게임 중엔 이미 상주) | 낮음 |
| B1. Stage2 제거, 디렉터 goal 사용 | 스토리 켜진 상태에선 `directive.goal` 이 이미 NPC 별 goal. `NpcPlans` 는 goal 만 채움(steps 미소비) | 첫 턴 -2.7~4.5s(최대 -10s), qwen3:8b 상주 제거, P5 피드백 루프 차단 | 0.5~1일: `dialogue.py:536-543`, UE `ShouldReplan`·`CurrentPlan` 소비처 확인 | **-5.2GB** | 스토리 밖 NPC(generic)는 goal 이 사라짐 → Jev `goal` 컨텍스트·replan 조건 영향. 사용자 결정 필요 |
| B2. Stage2 비동기화 | Stage1 응답을 먼저 보내고 plan 은 후속 메시지로 | 체감 -3~4s | 중: 새 Envelope 또는 별도 갱신 경로 → `python_backend.md` §1 의 3곳 동시 수정 | 0 | 응답 순서 비보장(`pitfalls.md` D "WS 동시 처리") 고려 필요 |
| B3. 유지 | `qwen3:8b` `keep_alive` 만 조정 | 소 | 0.1일 | +상주 | VRAM 악화 |
| C. latency hiding (bark) | 대기 0.8s 이상이면 UE 가 페르소나별 정형 한 줄·고민 제스처 즉출 | 체감 큼, 실제 지연은 그대로 | 중: 라인 데이터(오프라인으로 e4b 생성 가능)·몽타주 매핑·취소 처리 — 애니 에셋 엮임(memo 백로그와 동일) | 0 | 응답 도착 시 중복 발화 처리 |
| D. 스트리밍 | `speech` 가 JSON 앞이라 speech 완성 시점에 먼저 전달 | 체감 약 30~50%(추정: 출력 토큰 중 speech 비중) | 큰: 응답 2분할 프로토콜 | 0 | 액션 없이 대사만 먼저 → 연출 순서 설계 필요 |

### 3.4 P4 — VRAM

| 안 | 내용 | 효과 | 비용 | 위험 |
| --- | --- | --- | --- | --- |
| **A. 측정 먼저** | PIE 중 `ollama ps` + `nvidia-smi` 로 실제 상주 목록·여유 기록(이번 조사에서 미실행) | 이후 판단 근거 | 0.1일 | 없음 |
| B. Stage2 제거 (3.3-B1) | -5.2GB | 가장 큼 | 위와 같음 | 위와 같음 |
| C. Jev 를 CPU 로 | `JevlikeService(device="cpu")` — CUDA 컨텍스트 회피. 4패스 지연은 측정 필요(문서값 5~20ms 는 로컬 기준) | 수백 MB(추정) | 0.2일 + 측정 | 0.3s 워치독 여유 감소 시 되돌림 |
| D. `num_ctx` 4096→3072 | 프롬프트 다이어트(P6) 뒤에만 | KV 소폭 | 0.1일 | 잘림 재발. 프롬프트 길이 상한 테스트 동반 |

### 3.5 P5 — 대사 품질

| 안 | 내용 | 효과 | 비용 | 위험 |
| --- | --- | --- | --- | --- |
| **A. 기록 주입 정리 (권장)** | `get_context_string` 이 `Event` 를 분리: 대화 최근 2~3턴(Player/NPC 쌍) + 별도 `Recent events:` 최대 2줄. 5항목 상한은 유지 | 기록이 이벤트로만 차는 상황 해소, 문형 복사 완화 | 0.3일: `memory_manager.py:268`, `dialogue.py:228`, 테스트 | 낮음. 서빙 형식 변경이라 학습 분포와 약간 다름 |
| **B. 반복 감지 후 1회 재생성** | 새 `speech` 가 최근 NPC 발화와 정규화 후 동일/고유사면 temp 0.8 + 반복 문장 금지 문구로 재호출(생성기 `is_parrot` 와 같은 발상) | 로그 10-08 같은 글자 복사 차단 | 0.3일 | 드물게 +1.5~3s |
| C. RAG 정리 | 다국어 임베딩으로 교체(후보 `paraphrase-multilingual-MiniLM-L12-v2`·`multilingual-e5-small`, **미검증**), persona 카테고리 청크 제외(yaml 과 중복), `similarity_search_with_score` 임계값, 짧은 인사엔 RAG 생략 | 평균 -600~1,200자/턴 + 무관 로어 제거(P5 1단계 원인) | 0.5~1일: `rag_utils.py`, 벡터스토어 재빌드(`python -m app.story.seed`), 새 모델 다운로드 | 임베딩 모델 추가 용량·CPU 시간, 재빌드 필요 |
| D. Stage2 goal 피드백 차단 | goal 입력을 Stage1 발화가 아니라 persona+디렉터로(또는 B1) | 오염 goal 되먹임 소멸 | 3.3-B1 에 포함 | |
| E. v4 재학습 | `FINETUNE_v4_requirements.md` 의 `plan_achieved`·액션 절제·호감도 반영·반복 억제 | 근본 | P1-C 와 합쳐 한 번에 | 위 3.1-C 참조 |
| F. `plan_achieved` 우회 | 모델이 항상 false 이므로 서버가 `Dialogue` 이후 규칙(예: `GiveItem` 성공)으로 판정 | `MaxTurnsPerPlan` 의존 감소 | 0.5일 | 규칙 커버리지 한계 |

### 3.6 P6 — 프롬프트 다이어트 (본문 2,906자는 건드리지 않는다 — 학습 분포)

| 안 | 내용 | 절감(추정) | 비용 | 위험 |
| --- | --- | --- | --- | --- |
| **A. 조건부 주입 (권장)** | 파티 어휘 꼬리는 초대/해산 키워드·일행 존재 시만(3.1-A 감지 재사용), POI 안내는 이동·장소 키워드 시 상한 8→4(근처 설명 3줄은 유지), plan 래퍼 한 문장으로 단축 | 매 턴 약 400~700자 | 0.3일: `dialogue.py:331-333`, `interface_input.py:233-258,185-187` | 키워드 미탐 시 어휘 미노출 → POI/파티 미동작. 3.1-A 사후 보정이 보완 |
| B. 꼬리 위치·라벨 | 꼬리를 history 뒤가 아니라 `Recent memory` 앞에 라벨 블록으로 | 토큰 변화 없음, 기록 혼동 완화 | 0.1일 | 학습 분포 변화(소) |
| C. `valid_targets` 정리 | 적·자기 자신 제외(공격 대상은 `Enemy` 센티넬로 충분) | 수십 자 + 오지정 감소 | 0.3일: UE `NPCManager.cpp:340-345` 또는 서버 | 적 대상 NPC 명시 공격("DemonLord 를 공격") 필요 시 회귀 |
| D. RAG 조건부 | 3.5-C | 600~1,200자 | 위 | 위 |

### 3.7 P7 — Jev 체크포인트

| 안 | 내용 | 비용 | 위험 | 비고 |
| --- | --- | --- | --- | --- |
| A. `JEV_CHECKPOINT` | 워크트리·타 PC 는 `.env`/환경변수로 메인 트리 파일 지정 | 코드 0 | 경로 의존, 팀 확장 시 취약 | 지금 당장 워크트리에서 모델 경로 검증 가능 |
| **B. 체크포인트 추적 (권장)** | `.gitignore` 에 `!…/app/models/jevlike_tactics.pt` 예외 추가 후 0.5MB 파일 커밋 | 0.1일 | 재학습마다 0.5MB 이력, 공개 저장소 여부 결정(memo 에 보류 중) | 구형 `_m2`/`_v2c` 는 제외 |
| C. 재현 스크립트 | 추적된 `combos_*.jsonl` → `sweep.py`(build→jevlike.train) 한 줄 문서화 + 학습 설정(sharpen3·bmax4·w128) 고정 | 0.3일 | GPU 필요 | B 의 보완 |
| D. 전투 모델화 | soft 5단계 등급 라벨 재생성·재학습 후 `_model_probs or heuristic` 복귀 | 1~2일(라벨 31b 호출 포함) | 포위 aggressive 0% 노이즈 재발 가능 | memo 선택 항목. 헤드셋 체감 데이터 없이는 보류 권장 |

### 3.8 P8 — Jev 잔여

| 안 | 내용 | 비용 | 위험 |
| --- | --- | --- | --- |
| **A. 기동 시 워밍 (권장)** | lifespan 에서 `asyncio.to_thread(get_jev_service)` 로 torch import·체크포인트 로드를 미리 수행 + 로드 결과(`model_loaded`, 경로)를 INFO 로 명시 | 0.1일: `main.py` | 없음 |
| B. 관측 | 디버그 라우트 또는 `/api/ws/status` 류에 `jev: model|heuristic` 노출 | 0.1일 | 없음 |
| C. 실플레이 로깅 | daily 요청 metrics·결정(활동·슬롯·passes)을 `train_logger` 에 추가 → 활동 분포·낯선 POI id 점검, 향후 재라벨/재학습 입력 | 0.3일 | 로그 용량(날짜별 회전 이미 있음) |
| D. 사람 골드 채점 | `gold_sheet.html` 50건 → `eval`(memo 잔여 ①④) | 사용자 시간 | 편중 보완 시트 필요 |
| E. 학습 POI 어휘 정렬 | 합성 `POI_NAMES` 를 운영 11개로 확장 후 재라벨·재학습 | 0.5일 + 26b 라벨 | 모델 교체 시 골드 재채점 |
| F. 대체 모델 검토 | memo "Jevlike 대체·보강 후보군" — `jevbetter`(포맷 호환) 등 | 1일+ | 현 모델이 0.19~0.5MB·5~20ms 로 충분. **지금은 비권장**(D 의 사람 채점 후 판단) |

---

## 4. 권장 우선순위

| 순위 | 항목 | 다음 한 걸음 |
| --- | --- | --- |
| 1 | **P1-A 서버 의도 매핑** (+ 파티원 `Stop`→`Idle` 보호) | `dialogue.py` `_gate_party_actions` 앞에 키워드 매핑 함수 추가, `interface_input.py` 에 초대 힌트 한 문장 추가, `tests/test_party_update.py` 에 "합류"·"멈춰" 케이스 — 0.5일, 재학습 없음 |
| 2 | **P5-A/B + P6-A** (기록·반복·꼬리 정리) | `memory_manager.py::get_context_string` 에서 `Event` 분리, `dialogue.py` 에 직전 NPC 발화 반복 감지 1회 재생성, 파티 꼬리를 키워드 조건부로 |
| 3 | **P3-A 지연** (+ P4-A 측정) | `llm_factory.py:133` e4b `keep_alive` 연장 + WS 연결 중 주기 핑, PIE 중 `ollama ps`/`nvidia-smi` 한 번 기록. 이어서 Stage2 처리(§5-2) 결정 |
| 4 | **P7-B + P8-A/B** (Jev 체크포인트·워밍·가시성) | `.gitignore` 예외로 `jevlike_tactics.pt` 추적, lifespan 에서 `get_jev_service` 워밍, 로드 상태 로그/디버그 노출 |
| 5 | **P2-A/C → v4 재학습 판단** | 두 생성기 `story_goal` 수리 + `stage1_bench.py` 에 파티·POI·`--seed-history` 케이스 추가 → 숫자를 보고 3.1-C 착수 여부 결정 |

순서 근거: 1~4 는 재학습 없이 각 0.1~0.5일이고 서로 독립이다. 5 는 1~2 를 해도 남는 문제(간접 표현, 액션 절제, `plan_achieved`)를 숫자로 확인하기 위한 준비이며, 재학습(3.1-C/3.5-E)은 거기서 결정한다.

---

## 5. 사용자 결정이 필요한 것 (임의 진행 금지, `AGENTS.md` §4)

1. **"같이 가자" 단독을 합류로 볼 것인가** — 학습상 `Follow`(잠깐 따라오기)였다. 합류는 호감도 게이트·Jev 일상 억제·추적 영구화를 동반하므로 오탐 비용이 크다. 선택지: ① 명시 키워드(합류·일행·파티·동료·팀)만 ② "같이 가자"도 포함.
2. **Stage2(`qwen3:8b`) 의 운명** — 유지 / 스토리 goal 로 대체(제거) / 비동기화. 제거 시 스토리 밖 NPC 의 plan·`ShouldReplan`·Jev `goal` 이 어떻게 되는지 UE 쪽 확인 필요.
3. **Jev 체크포인트 git 추적** — 공개 저장소 공개 여부(memo 에 보류 기록).
4. **v4 재학습 착수 여부와 범위** — P1-C 와 3.5-E 를 한 번에 할지, 이번엔 서버 쪽 보정만 할지.
5. **RAG 임베딩 모델 교체 허용** — 새 모델 다운로드·벡터스토어 재빌드 동반.

---

## 6. 조사 한계

- PIE·서버를 띄우지 않았다. 지연·품질 수치는 `train_logs`(Stage1 v1 응답: 2026-09-18 이후 46건, 유휴 뒤 첫 턴 5건)에 근거하며 표본이 작다.
- `ollama ps`/`nvidia-smi`/`ollama list` 를 보지 않아 실제 상주 모델·정확한 VRAM 은 모른다.
- 로그의 `extra`(npc_id 등)가 평탄화돼 NPC 별 필터링은 프롬프트 본문으로 했다.
- 토큰 환산, 임베딩 모델 후보 성능, 재학습 소요(2~4일)는 추정이다.
- 학습 원본(721행)이 없어 v1 의 정확한 의도 분포는 생성기·의도표(`intents/*.yaml`)로만 확인했다.
