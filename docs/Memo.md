# Session Memo

> 세션 간 인수인계용 메모장. 형식: `docs/CLAUDE.md` → Session Memo 섹션 참조.
> 완료 항목은 `docs/주간기록/` 개발일지로 이관 — 여기엔 미완(Todo)·맥락(Handoff)만 남긴다.

---

## Todo

브랜치: `feature/MetaQuest3S-VR`
목표: PC 전용 플레이어 + NPC AI + ChatWidget을 Quest 3S(스탠드얼론 또는 PCVR Link) + 음성 TTS 로 동작시키기

### 🆕 다음 설계 작업
- [ ] **대화 UI(ChatWidget) 부활** — 현재 응답은 화면 자막(AddOnScreenDebugMessage)뿐. WorldSpace ChatWidget 또는 자막 위젯으로 정식화.

### Quest 스탠드얼론 빌드 — 보류 (현재 PCVR Link 로 개발/데모)
**결정(2026-05-30)**: 현재 Quest Link(케이블 PCVR 스트리밍)로 개발·테스트 중 — 게임은 PC에서 돌고 Quest는 디스플레이. 이 모드에선 APK·사이드로딩·IP 외부화 전부 불필요(서버도 PC라 127.0.0.1 OK). 아래 항목은 **최종 타겟을 Quest 단독 구동으로 확정할 때만** 진행. 그땐 모바일 성능 최적화 숙제도 동반.
- [ ] (스탠드얼론 확정 시) `Project → Package → Android (ASTC)` 빌드 → APK
- [ ] (스탠드얼론 확정 시) `adb install <APK>` 사이드로딩
- [ ] (스탠드얼론 확정 시) LLM/TTS 서버 IP 외부화 → `DefaultGame.ini` [OmniAgent] (현재 127.0.0.1)
- [ ] (필요 시) AndroidManifest 에 `CHANGE_NETWORK_STATE` 추가 — 현재 INTERNET 만 등록됨

### VR UI 적응
- [ ] ChatWidget 스크린스페이스 → WorldSpace 전환 (WristWidgetComp, BP_ChatWidget "Is Variable" → World Space)
- [ ] ChatWidget 폰트 크기 VR 용 확대
- [ ] 텍스트 입력 — Meta Quest 시스템 키보드 (`UMetaXRInputFunctionLibrary::ShowVirtualKeyboard()`, MetaXR)
- [ ] WorldSpace 위젯 레이캐스트/손가락 상호작용 셋업 (PDF 설계서 §7.2): ① Widget Trace Response Ignore→**Block** ② `Set Input Mode Game and UI` (하이브리드 포커싱) ③ Widget Component `Receive Hardware Input` 활성

### TTS 잔여
- [ ] 지연 측정 수치 기록 — T0(PlayFromUrl)→T1(WS Connected)→T2(첫 청크)→Play() 실측, 목표 < 600ms

#### M3 — 운영 강화
- [x] TTS 타임아웃·재시도 정책 — tts_client 0.2s 백오프 1회 재시도, 실패 시 자막 fallback (2026-06-05)
- [x] `request_id` 기반 로그 trace — msg_id→request_id 상속, `[trace=]` 로그태그 (main.py·tts_client·server.py) (2026-06-05)
- [ ] 동시 발화 1–2 NPC 큐잉 검증 (런타임 검증 필요)
- [ ] LLM 스트리밍 도입 여부 재검토 — 도입 시 문장단위(마침표/쉼표) 조기 전송 + 청크 합성
- [ ] 청크 이음새 튀는 소리 제거 — `cross_fade_duration=0.15` 적용 (PDF 설계서 §6.2)

#### M4 (선택) — 표정·제스처 동기화
- [ ] MetaHuman Lip Sync 또는 Audio2Face 에 오디오 스트림 분기
- [ ] AnimBP 에 `LipSyncCurve` / `FacialExprParams` / `GesturePose` 입력 추가
- [ ] `animation_metadata.emotion` → AnimBP `EmotionMood` 파라미터 주입
- [ ] (별도 브랜치 R&D) StreamingTalker / Audio2Gesture / CAP4D

### 음성 입력 (ASR)
- [x] `ASRService` (faster-whisper large-v3, WS, **final** 결과) — 동시추론 Lock·버퍼상한·프리워밍 (2026-06-05). README 추가.
- [x] UE Mic Capture (`UVoiceInputComponent`, PCM s16le → ASR WS) (2026-06-05)
- [x] ASR→NPCManager→LLM 배선 검증 (2026-06-05): IA_VoiceInput→StartTalking→ASR→`OnTranscriptReady`→`HandleVoiceTranscript`→`SendPlayerDialogue`→BuildPrompt. 포트(8002)·PromptPayload(voice_transcript/target_npc_id)·main.py 파싱 전부 일치. **코드 완결**.
- [ ] **ASR partial text 스트리밍** (Sprint 3): 서버가 부분 인식 결과를 주기 송신, `HandleAsrMessage` 의 `partial`/`ready` 처리(현재 무시) + 월드위젯 타이핑 표시. PDF §8.3.
- [ ] (전환 완료 후) ChatWidget 채팅 기록 코드 정리 또는 레거시 모드로 분리 (에디터)

### RAG 지식 충전 (NPC 페르소나 미로드 — `No documents found for Skadi`)
- [x] 파이프라인 완성 (2026-06-05): `rag_utils` chunk_category(lore/persona/history) 메타 태깅, 재빌드 CLI `python -m app.utils.build_knowledge`, 작성 템플릿/가이드 `knowledge_template/`. end-to-end 스모크 검증됨.
- [x] 폴더 정합 (2026-06-05): `elera`→`elara` 통일(persona+knowledge), stale vectorstore(elara/james) 삭제. NPC별 서브폴더 스캐폴드.
- [ ] **(사용자) 실제 lore/persona/history 텍스트 작성** — `knowledge/<npc>/{lore,persona,history}/*.md`. skadi/elara/james 폴더 비어있음("No documents" 원인). `knowledge_template/_AUTHORING_GUIDE.md` 참조 후 `build_knowledge --all`.
- 참고: 임베딩 로컬 `all-MiniLM-L6-v2`(클라우드 금지), npc_id 필터는 NPC별 스토어 분리로 달성. memory_manager.py 는 대화 히스토리(JSON)용 — RAG 인제스트 아님(PDF 혼동).

### Phase 6 — VR 자세 동기화 멀티플레이어 (우선순위 낮음)
- [ ] AVRPawn 리플리케이션 — bReplicates=true, ReplicateMovement, Component Replicates
- [ ] EVRPosture `ReplicatedUsing=OnRep_Posture` — Enum 만 전송 (저주파, RPC 파라미터)
- [ ] HMD/양손 Transform VROrigin 기준 Relative 직렬화 후 Multicast RPC — World 좌표 금지
- [ ] 캡슐/VROrigin = CharacterMovementComponent 서버검증 복제 / HMD·손 = 상대벡터만 ~0.25s 틱 RPC (계층 분리)
- [ ] OnRep_Posture 에서 SetCapsuleHalfHeight + VInterpTo(위치)·**RInterpTo(회전)** 보간 — Snap 금지, 원격 클라 자체 실행
- [ ] Remote proxy 카메라 컴포넌트 `DestroyComponent` (`!IsLocallyControlled` 확인) — 렌더/오버헤드 차단
- [ ] 참고: PDF 설계서 §7 (상대좌표 복제 / World UI)

### BehaviorTree → StateTree 마이그레이션 (잔여, 에디터)
- [ ] `ST_NPC.uasset` Transition 조건(`bHasAction==true`) 및 Evaluator 연결 확인
- [ ] `BB_NPC.uasset` 에서 HasAction / SubAction / BehaviorMode / FacialState 키 제거 (ST 에셋 확인 후)

### DA_NPC_Actions 몽타주 등록 (에디터) — 🔴 최우선(이게 비면 LLM 행동 절반 무음실패)
- [ ] `Block` / `Dodge` / `SitDown` / `SitUp` / `LieDown` / `LieUp`
- [ ] `Give` / `PickUp` / `Drop` / `Eat` / `Comfort` / `Craft` / `Repair` / `Pray` / `Read`
- [ ] `Emote_*` / `Dance_*` / `Sing_*` — LLM 스타일 키 (대표값 사전 등록)

### 에디터 잔여 (빌드 후 / 에셋 연결)
- [ ] **EQS Named Parameter 바인딩** (빌드 후 필수): `DistanceWeightParam`/`CoverWeightParam`→Test Score Factor, `SafeDistance`→Distance Filter Min, `AggressionWeightParam`→TacticalPositionsQuery Inverse Distance Test (Generator 반경은 고정값). 안 하면 전술 가중치 무효.
- [ ] **BP CDO `PerceptionTickInterval` 리셋**: C++ 기본 9.0f인데 BP CDO가 옛 3.0f 직렬화 중. NPC BP Details에서 ↺ 리셋 후 저장.
- [ ] **`IA_VoiceInput` 에셋 할당 확인**: `BP_VRPawn`의 IA 프로퍼티 + IMC 매핑(push-to-talk). 코드 배선은 완료, 에셋 연결만 에디터.
- [ ] (PR #9 머지 후) **UE 풀빌드 1회** — 이번 세션 C++ 변경(퍼셉션 StaticClass, SetTimer TWeakObjectPtr×3 등) 빌드 검증.

---

### 알려진 제약
- WebSocket ws:// 연결: Quest 와 PC 가 같은 LAN 필요 (인터넷 라우팅 없이)
- ChatWidget 텍스트 입력: Quest 가상 키보드 API 별도 통합 필요
- 손 메시 애니메이션: MetaXR HandPoseRecognition 연동 별도 작업
- SyncCapsuleToHMD: Z축 처리는 계단/경사로 등 지형에 따라 튜닝 필요
- `bPackageForMetaQuest=True` 가 Meta 전용 매니페스트를 추가 — Quest 외 안드로이드 기기에는 설치 안 될 수 있음

---

### 멀티 NPC 병렬 대화 파이프라인 (2026-06-12)
- [x] Dialogue 3-Stage 멀티 NPC 전환 — Stage1: e4b×N 병렬(`asyncio.gather`, 지식 격리), Stage2: 12B×1 스타일 정제(NPC 2개↑ 시), Stage3: interface_output e4b 구조화. `state.py` `raw_responses`/`action_batches` Dict 필드 추가 (2026-06-12)
- [x] interface_output async 전환 + `_correct_facial_contamination` — trait↔FacialState 모순(감정 수렴 오염) 결정론적 Python 보정 (2026-06-12)
- [x] rules/supervisor/main 멀티 배치 대응 — `action_batches` Dict 루프 검증, 거부 판정/폴백 멀티화, `ModeActionRequest.ActionBatches` 다중 전송 (2026-06-12)
- [x] interface_input 멀티 NPC 추출 — `_extract_target_npcs` 전체 매칭(등장순·중복제거·상한 3), Player 제외 (2026-06-12)
- [ ] 멀티 NPC end-to-end 런타임 검증 — `OLLAMA_NUM_PARALLEL=3` 설정 후 2~3 NPC 동시 발화 실측

### 인지엔진 지연 최적화 (2026-06-12 분석)
- [x] SLM Reflex raw few-shot 전환 — thinking 잘림(빈 응답→Scan 고정) 해소. 실측 warm 330ms, 적대 케이스 Attack 정상 (2026-06-12)
- [x] WS 메시지별 태스크 분리 — 대화 처리 중 location_decision/emergency 큐 묵힘 해소. `_ws_send_lock` 송신 직렬화 (2026-06-12)
- [x] 대화 응답이 메모리 요약 LLM에 동기 차단 — `dialogue.py` `add_conversation` fire-and-forget 분리, 백그라운드 착지 검증 (2026-06-12)
- [x] normal NPC 대화 모델 e4b(thinking) 재검토 — `get_llm` ChatOllama `reasoning=False` 전역 적용으로 해소 (2026-06-12)
- [x] DIALOGUE_SYSTEM_PROMPT prefix 재배치 — 정적 규칙 앞 / 동적 페르소나 뒤. NPC 교체 호출 prompt eval 183ms→37ms 실측 (2026-06-12)
- [x] core 모델 26b→`gemma4-12b` 교체 (OBLITERATED Q4_K_M 별칭, VRAM 16GB 적합) — llm_factory·main.py·debug.html·README 동시 수정 (2026-06-12)
- [x] LangGraph Rules 거부 처리 복원 — Rules→Supervisor 엣지(데드코드였던 재시도 분기 활성화) + `rules_retry_count` 1회 제한, 소진 시 폴백 배치(빈 배치 UE5 전송 차단) (2026-06-12)
- [x] failed_action_history LLM 주입 — interface_input 이 최근 3건을 natural_context 에 포함("do NOT retry the same way"), 반복 실패 차단 (2026-06-12)

## Handoff Notes

- **멀티 NPC 병렬 설계 결정 (2026-06-12)**: 여러 NPC를 한 Dialogue 호출에 묶지 않고 NPC별 e4b 병렬 호출로 분리. **Why**: 단일 호출로 모든 페르소나·RAG를 같은 컨텍스트에 넣으면 ① 감정 수렴 ② **지식 누출**(A만 아는 비밀을 B가 발화) 오염 발생 — 지식 누출은 발화 귀속 추적이 불가능해 사후 교정 못 함. 병렬 별도 호출은 지식 격리가 구조적으로 보장됨. `asyncio.gather`라 순차 대비 지연 동일(단 Ollama가 큐 처리하면 직렬화 — `OLLAMA_NUM_PARALLEL=3` 필요, 미설정 시 기능은 동작하되 병렬 효과 없음). **How to apply**: ① Stage2 12B 정제는 "스타일만, 사실 추가 금지" 프롬프트 — 정제 단계서 지식 섞일까 우려되면 Stage2 스킵 가능(단일 NPC는 이미 스킵). ② 감정 수렴 오염은 `interface_output.py::TRAIT_EMOTION_MAP`로 결정론적 보정(Aggressive NPC가 Fear로 수렴→Angry 복원) — trait 맵에 없는 NPC는 보정 안 됨, 새 페르소나 trait 추가 시 맵 갱신. ③ 동시 NPC 상한 `MAX_TARGET_NPCS=3`(interface_input) — 토큰 폭발/오염 방지, 늘리면 12B 정제도 불안정. ④ 전 파이프라인 단일 NPC 호환 경로 유지(`action_batch`/`raw_response` 단수 필드 병행 채움) — 기존 emergency_report/SLM reflex 경로 안 깨짐.
- **gemma4-12b 별칭 + thinking 비활성 (2026-06-12)**: core 대화 모델은 `gemma4-12b` — `ollama cp hf.co/mradermacher/Gemma-4-12B-OBLITERATED-GGUF:Q4_K_M gemma4-12b` 로 만든 로컬 별칭. Ollama 재설치 시 pull 후 cp 재실행 필요. `get_llm` 의 ChatOllama 에 `reasoning=False` 전역 적용 — 12B 실측에서 thinking 이 num_predict 200 전부 잠식해 content="" 발생, think=false 로 663ms 정상 응답. gemma4/qwen3 계열 전부 thinking 모델이라 대화 3티어 공통 적용. 비-thinking 모델(llama3.3 레거시 폴백)을 쓰게 되면 Ollama 가 think 파라미터 거부할 수 있음 — 그때 분기 추가. 26b 는 코드 참조만 제거, 디스크엔 잔존(17GB) — `ollama rm gemma4:26b` 는 사용자 판단.
- **WS 메시지별 동시 처리 (2026-06-12)**: `websocket_llm_endpoint` 가 메시지마다 `asyncio.create_task` 로 분리 처리 — 응답 순서 비보장. prompt 는 msg_id, location_decision 은 request_gen 으로 수신 측 매칭이라 순서 의존 없음. 같은 소켓 동시 쓰기는 `_ws_send_lock` 으로 직렬화(TTS 푸시·디버그 명령 포함). stale 임계 10초는 유지 — 직렬화 큐잉이 원인이던 지연이 사라졌으므로 실측 후 하향 검토 가능.

- **PR #9 Gemini 보류 항목 (2026-06-05)**: 티키타카 5라운드로 ~20건 반영했으나 아래는 의도적 보류 — 빌드/런타임 검증 필요, 크래시 아님(perf/동작). **How to apply**: UE 풀빌드 후 실측하며 판단. ① `ItemManager::GetItemsInRange` O(N×M) → 역참조 맵 / `StaticLoadObject` 동기로드 hitch → 비동기 로드. ② `NPCStateComponent::FlushEventReport` WaitingLLM 조기반환 시 지연 이벤트가 재flush 안 돼 영구대기 가능 — TacticalQueryState 해제 시 재처리 트리거 필요. ③ `VRPawn` ShotDirection 이 Grip 포즈 forward 인데 조준선은 Aim 포즈 — 무기 명중 방향 불일치. **player_id 는 오탐**(vr_context.player_id 정상, 건드리지 말 것).
- **VR PCVR(Link) 결론 (2026-05-30)**: 현재 Quest Link(케이블 PCVR)로 개발 — 게임은 PC 실행, Quest는 디스플레이. **APK/사이드로딩/IP외부화 불필요**(서버도 PC, 127.0.0.1 OK). 타이틀바 `OpenXR Oculus`+Link 가 PCVR 증거. **How to apply**: 스탠드얼론(Quest 단독 언테더드)을 최종 타겟으로 확정하기 전엔 Memo "Quest 스탠드얼론 빌드" 항목 손대지 말 것. 확정 시 APK + IP외부화 + 모바일 성능 최적화 동반. VR 아바타 IK 자체는 완성·머지됨.
- **VR FBIK 튜닝값 (2026-05-30)**: X_Bot 기준 C++ 기본값 박힘 — `LeftHandGripOffset(180,0,90)`, `RightHandGripOffset(0,0,-90)`, `HeadEffectorOffset(0,-90,90)`, `CameraHeightOffset=-30`, `AvatarReferenceHeight=170`. **Why C++ 기본값**: CLAUDE.md §9(에디터 수작업 최소화) — 바이너리 uasset 대신 소스에 명시해 버전관리·인수인계. **How to apply**: 다른 스켈레톤 쓰면 이 값들 재튜닝 필요. `LogIKMetrics` exec 로 이펙터 Transform 덤프 → CR 변수 Default 에 박아 프리뷰에서 PIE 로딩 없이 튜닝(이번 워크플로우). 팔꿈치는 CR_VRPawn_FBIK Bone Settings Preferred Angle.
- **Quest 빌드 보류 (2026-05-16)**: USB-C 케이블 미보유로 실기 사이드로딩 불가. **현재 코드는 다 준비됨** — VR Phase 1·2 (OpenXR·VRPawn·BP_VRPawn·Android 패키지 설정·OBB 통합·Vulkan·arm64) 모두 Develop 머지 완료. 케이블 도착 즉시 위 "Quest 빌드" 체크리스트 실행하면 됨. **잊지 말 것**: ① 케이블 도착 알림 / ② Android SDK 머신 설정은 .ini에 없으니 새로 셋업 필요 / ③ Quest 와 PC 같은 Wi-Fi 확인.
- **gemma thinking 우회 패턴 (2026-05-16)**: gemma3/4 instruct 모델은 chat template 안에 thinking(reasoning) 토큰 생성이 강제됨. langchain `ChatOllama.invoke` 로 호출하면 항상 chat template 적용 → 단답 prompt 도 200+ thinking 토큰. **해결**: `httpx` 로 Ollama `/api/generate` 직접 호출 + `raw=true` (chat template 우회) + few-shot prompt (Answer: 까지 채워주면 모델은 다음 한 단어만 생성). **측정 비교**: 기존 2200ms/289토큰 → 새 400ms/4토큰. **How to apply**: ① `num_predict` 작게(10) + `stop=["\n"]` 로 안전 마진. ② raw 모드는 instruction 튜닝 효과 약화 — 단답형/분류 작업에만 사용, 자유 대화는 chat template 유지. ③ keep_alive="5m" 로 모델 메모리 유지(cold-start 회피). ④ `request_timeout` 20초로 cold-start 6초 마진.
- **EQS request_gen 프로토콜 (2026-05-16)**: location_decision payload 에 `request_gen` (UE5 → Python echo → UE5) 필드 추가. UE5 `TacticalQueryGeneration` 은 `StartTacticalQuery` / `AbortTacticalQuery` 마다 증가, `NotifyLocationDecisionReady` 가 호출되면 echo 받은 값과 비교해 stale 응답 무시. **Why**: 후보 ID 가 `SAFE`/`OPTIMAL`/`AGGRESSIVE` 3개 카테고리 고정이라 신/구 응답을 ID 만으로는 구분 불가 — 옛 응답이 우연히 새 CandidateMap 의 같은 ID 와 매칭되어 잘못된 좌표를 사용할 위험 있었음. **How to apply**: ① `request_gen=0` 은 stale 검사 우회(레거시 메시지 호환). ② Python 은 단순 echo — 검증/저장 책임 없음. ③ 다른 비동기 응답형 메시지(예: TTS request_id)에서도 같은 패턴 재사용 권장.
- **gemma SLM thinking 토큰 주의 (2026-05-16)**: `gemma4:e4b` (location_decision SLM)는 답 토큰 전에 사고(reasoning) 토큰을 흘리는 thinking 모델. `num_predict=20` 으로 호출하면 추론 중간에 잘려 `response=""` + `done_reason="length"` 로 떨어짐 (Ollama 직접 호출로 검증). **How to apply**: ① 단답형 응답이라도 num_predict 200+ 확보. ② 새 LLM 호출 추가 시 모델별 thinking 여부 확인 필수 — gemma3/4 e/p 시리즈는 thinking 활성. ③ 만약 사고 토큰 자체가 비용 부담이면 `/no_think` 같은 시스템 프롬프트 또는 chat template 강제(`<start_of_turn>model\nANSWER:` 처럼 직접 답 유도) 검토. ④ 재시도 로직은 동시성/네트워크 결함에만 의미가 있으므로 유지하되 num_predict 가 1차 진단 포인트.
- **자동 Track 주입 제거 (2026-05-16)**: `STTask_PrepareNextAction.cpp` 비전투 자동 Track 블록 삭제. **Why**: BB.TargetActor 가 채워지면 ST 가 매 tick "할 일 없네? Track 주입" → 0.5s 타이머가 MoveToActor 반복 → NPC 가 플레이어를 무한 따라다님. 거리/시야 이탈 해제 로직도 없었음. **How to apply**: ① 비전투에서 NPC 가 시야에 들어와도 가만히 있는 게 정상. 추적이 필요하면 LLM 이 Follow/Track 액션을 명시해야 함. ② Combat 모드의 Attack 자동 주입은 유지(전투 반응성). ③ "NPC 가 너무 정적"이라는 피드백 오면 LLM 프롬프트 / 또는 거리·시야 조건부 Track 재도입 검토.
- **TTS dispatch 정책 (2026-05-16)**: `main.py::_handle_prompt` 의 `"NPC_Debug"`/`"Skadi"` 하드코딩 폴백 제거. **Why**: Python 은 UE5 NPCMap 상태를 모름 — 임의 폴백은 잘못된 NPC 가 발화할 위험. **How to apply**: ① 우선순위는 `final_action.AgentID` → `target_npc_from_payload`. 둘 다 없으면 dispatch skip + `[Main][TTS] target_npc 미지정` 로그. ② `target_npc_id` 는 VRPawn perception 결과를 Envelope 에 실어 전송. NPC 안 보고 prompt 보내면 발화 대상 없음이 정상. ③ M2(LLM 이 dialogue_text 산출) 단계에서 `final_action.AgentID` 가 항상 채워지면 자연 해결.
- **NPCAudioStreamComponent 지연 측정 로그 (2026-05-16)**: `[NPCAudio][T0]` PlayFromUrl → `[T1]` WS Connected +ms → `[T2]` 첫 청크 → Play() +ms. M2 본체 도입 후 첫 음 지연 목표 < 600ms 검증용. 헤더에 `HandleConnected()`, `PlayRequestedAt` 추가.
- **Quest Phase 2 .ini 결정 사항 (2026-05-13)**: Package=`com.jukanmi.ue5mcpvr`, Min SDK 32 / Target 34, ASTC 단일 텍스처 포맷, OBB 통합(`bPackageDataInsideApk=True`), Vulkan + arm64 단독, `bPackageForMetaQuest=True`. **Why**: Quest 3S 단독 타깃 가정. **How to apply**: ① NDK r25c·JDK 17·Android SDK 32·34 설치는 머신별이라 .ini 에 없음, UE Editor → Project Settings → Android SDK 에서 직접 지정. ② `bPackageForMetaQuest=True` 가 일부 Meta 전용 매니페스트를 추가 — Quest 외 안드로이드 기기에는 설치 안 될 수 있음. ③ ASTC 단일이면 PC 에디터에서 모바일 프리뷰 시 텍스처가 느리게 빌드될 수 있음, 평소 작업은 Desktop 프리뷰로.
- **TTS M1 스텁 구조 (2026-05-13)**: TTS 흐름은 `명세.md` §5.3 + `vibevoice-dev-spec.md` + `docs/TTS_Integration_Plan.md` 3개 문서가 정본. **How to apply**: ① NPC BP(예: BP_SmartNPC)에 `NPCAudioStreamComponent` 를 수동 첨부해야 재생됨. ② TTSService 는 별도 프로세스(`python -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001`).
- **PC/VR 입력 에셋 폴더 분리 (2026-05-09)**: `Content/Core/Input/` 루트의 IA_*.uasset 7종을 `Computer/` 하위로 이동했고, VR용은 `VR/` 폴더에 신규 생성. **Why**: 데스크톱 빌드와 Quest 빌드가 다른 IMC를 쓰는데 파일명이 충돌하면 IMC가 잘못된 IA를 참조할 위험. **How to apply**: 기존 `BP_Player`/`BP_VRPawn`에서 IA 참조 경로가 깨졌을 수 있음 — 두 Pawn BP 컴파일 후 IA 레퍼런스 누락 경고 확인 필수.
- **EQS Named Parameter 바인딩 위치**: Generator의 Search Radius는 UE5 EQS 에디터에서 Named Parameter로 바인딩 불가. **Test 노드의 Score Factor**에만 바인딩 가능. C++의 `SetFloatParam("DistanceWeightParam"/"CoverWeightParam")`은 Test 가중치에서만 의미가 있음. Generator 반경은 에셋에 고정값으로 둘 것.
- **EQS 에디터 작업 미완 (빌드 후 필수)**: DistanceWeightParam/CoverWeightParam → Test Score Factor, SafeDistance → Distance Filter Min, AggressionWeightParam → TacticalPositionsQuery Inverse Distance Test에 Named Parameter 바인딩 필요.
- **Blueprint CDO PerceptionTickInterval 리셋 필요**: C++ 생성자에 9.0f 추가했으나, 이미 저장된 BP CDO가 3.0f를 직렬화하고 있음. 에디터에서 NPC Blueprint → Details → PerceptionTickInterval 옆 ↺(Reset) 버튼 클릭 후 저장/재컴파일 필요.
- **LLM 모델 라우팅**: importance=normal→gemma4:e4b, high→gemma4:12b, core→gemma4:26b. 모델 변경 시 `dialogue.py`/`main.py`/`debug.html` 세 곳을 동시에 맞출 것.
- **NPC 시작 시 따라오는 버그**: ① `OnTargetPerceptionUpdated` — 중립 대상(FinalDanger<0.5)은 전술 쿼리 미발동. ② `STTask_PrepareNextAction` — Combat 모드일 때만 전술 쿼리 발동. Python DB 호감도 잔류 시 디버그 대시보드(http://127.0.0.1:8000/debug)에서 초기화 필요.
- **Track 몽타주 없음**: `ExecuteTrack`은 이동(MoveToActor 0.5s 반복)만 사용. DA_NPC_Actions "Track" 키 등록 불필요.
- **code-review-graph 오탐 패턴**: anonymous namespace 함수(EvalSafeScore 등), FastAPI 라우트, LangGraph 노드, C++ 매크로(UE5_MCP_VR_API)는 dead_code로 잘못 잡힘. 제거 전 반드시 grep으로 직접 호출 사이트 확인할 것.
- **JSON 키 폴백 제거 배경**: Python interface_output.py가 snake_case로 통일 완료됨. 혹시 Python 쪽에서 키가 안 오는 경우, MCPJsonUtils에 폴백을 추가하지 말고 Python 출력 스키마(actions.py)를 먼저 확인할 것.
