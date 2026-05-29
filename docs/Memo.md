# Session Memo

> 세션 간 인수인계용 메모장. 형식: `docs/CLAUDE.md` → Session Memo 섹션 참조.

---

## Todo

브랜치: `feature/MetaQuest3S-VR`
목표: PC 전용 플레이어 + NPC AI + ChatWidget을 Quest 3S 스탠드얼론 + 음성 TTS 로 동작시키기

### 🆕 다음 설계 작업
- [ ] **ASR(음성→텍스트) 입력** — 단순 대화의 발화 입력을 디버그 exec(`SendNPCDialogue`)에서 실제 마이크/Whisper 스트리밍으로 교체. send 진입점(`UNPCManager::SendPlayerDialogue`) 재사용.
- [ ] **대화 UI(ChatWidget) 부활** — 현재 응답은 화면 자막(AddOnScreenDebugMessage)뿐. WorldSpace ChatWidget 또는 자막 위젯으로 정식화.

### 🔥 지금 다음 액션 (사용자 직접, UE Editor)

#### Quest 빌드 — 케이블 도착 후 재개 (보류 중, 2026-05-16)
**현재 상태**: VR 코드(Phase 1·2)는 Develop에 머지 완료. 실기 빌드·사이드로딩만 남음. USB-C 케이블 도착하면 아래 순서대로.

- [ ] Project Settings → Platforms → Android SDK
  - NDK 경로(r25c) / SDK 경로(API 32·34 설치) / JDK 17 경로 — 머신별 설정이라 .ini 미포함
  - "Accept SDK License" 클릭
- [ ] Project Settings → Platforms → Android → Distribution Signing
  - Debug keystore 자동 생성 OK / Release 는 별도 keystore 준비
- [ ] (선택) `MetaXR` 플러그인 설치 — 가상 키보드·핸드 포즈 등 Meta 전용 기능 사용 시
- [ ] `Project → Package → Android (ASTC)` 빌드 → APK 생성
- [ ] Quest 개발자 모드 활성화 (Meta 계정 / Oculus 앱)
- [ ] ADB 또는 Meta Quest Developer Hub 로 APK 설치: `adb install <APK경로>` (**케이블 필수**)
- [ ] LLM/TTS 서버 IP 하드코딩 제거 → `DefaultGame.ini` [OmniAgent] 섹션 (현재 모두 127.0.0.1 — Quest 에선 PC IP 필요)

#### TTS 잔여
- [ ] 지연 측정 수치 기록 — T0(PlayFromUrl) → T1(WS Connected) → T2(첫 청크) → Play() 실측치, 목표 < 600ms

---

### 🆕 VR 자세 시스템 — 에디터 작업 (C++ Phase 1-3 완료, 2026-05-23)

계획서: `~/.claude/plans/vr-ancient-crown.md`. C++ 측은 `feature/MetaQuest3S-VR` 에 반영됨.

#### Phase 4 — BP_VRPawn 메시 부착 + AnimBP 생성
- [ ] `BP_VRPawn` → Mesh 컴포넌트에 `X_Bot` 할당
- [ ] Mesh Relative Z = `-88` (캡슐 기본 절반높이), Yaw = -90 (X+ 정렬, 빌드 후 확인하여 조정)
- [ ] 신규 `Content/Blueprint/Player/ABP_VRPawn` (Anim Blueprint, X_Bot_Skeleton)
  - `BlueprintThreadSafeUpdateAnimation` 오버라이드 (Event 그래프 X)
  - Property Access 로 `AVRPawn::CurrentPosture` / `CalibratedStandingHeight` / VRCamera·MotionController Transform 가져옴
  - AnimGraph 는 변수 직접 핀 연결만 (Fast Path 유지) — Cast/수식 금지
  - 작은 State Machine: Standing/Crouching/Prone (Enum 동등 비교 전이)
- [ ] Mesh → Anim Class = ABP_VRPawn 지정

#### Phase 5 — FBIK Control Rig
- [ ] 신규 `Content/Core/Animation/CR_VRPawn_FBIK` (Control Rig, X_Bot_Skeleton 기반)
- [ ] Full Body IK 노드 추가 — Root: `Hips`
- [ ] Effectors: Head (HMD), LeftHand (MC Left Grip), RightHand (MC Right Grip)
- [ ] Exclude Bones: Spine, Spine1, Spine2, Neck (PDF §척추 압축 방지)
- [ ] ABP_VRPawn AnimGraph 에 Control Rig 노드 추가 + Effector 트랜스폼 핀 연결

### Phase 6 — VR 자세 동기화 멀티플레이어 (별도 작업, 우선순위 낮음)
- [ ] AVRPawn 리플리케이션 설정 — bReplicates=true, ReplicateMovement, Component Replicates
- [ ] EVRPosture 를 `ReplicatedUsing=OnRep_Posture` 변수로 — Enum 만 전송 (저주파, RPC 파라미터)
- [ ] HMD/양손 Transform 은 VROrigin 기준 Relative 좌표로 직렬화 후 Multicast RPC — World 좌표 금지
- [ ] OnRep_Posture 에서 SetCapsuleHalfHeight + VInterpTo 보간을 원격 클라이언트 자체 실행 (Raw Z 미전송)
- [ ] 참고: PDF §"멀티플레이어 환경의 네트워크 리플리케이션 및 패킷 최적화 전략"

---

### Phase 3 — Quest ↔ PC 네트워크
- [ ] `WebSocketClient.cpp` — Quest 에서 PC Python 서버로 ws:// 연결 검증 (같은 Wi-Fi 필수)
- [ ] LLM/TTS 서버 IP 하드코딩 제거 → `DefaultGame.ini` [OmniAgent] 섹션에서 읽도록 수정
  - 현재 LLM=`ws://127.0.0.1:8000/ws/llm`, TTS=`http://127.0.0.1:8001` 모두 localhost 가정
- [ ] (필요 시) AndroidManifest 에 `CHANGE_NETWORK_STATE` 추가 — 현재 INTERNET 만 등록됨

### Phase 4 — VR UI 적응
- [ ] ChatWidget 스크린스페이스 → WorldSpace 전환 (WristWidgetComp 가 처리, BP_ChatWidget 의 "Is Variable" → World Space)
- [ ] ChatWidget 폰트 크기 VR 용으로 확대
- [ ] 텍스트 입력 — Meta Quest 시스템 키보드 연동 (`UMetaXRInputFunctionLibrary::ShowVirtualKeyboard()`, MetaXR 설치 후)

### Phase 5 — 빌드·사이드로딩
- [ ] `Project → Package → Android (ASTC)` 빌드
- [ ] Quest 개발자 모드 활성화 (Meta 계정 / Oculus 앱)
- [ ] ADB 또는 Meta Quest Developer Hub 로 APK 설치: `adb install <APK경로>`

---

### TTS 통합 다음 단계 (`docs/TTS_Integration_Plan.md` 참조)

#### M2 — 본체 도입 (OpenVoice v2 채택)
- [x] OpenVoice v2 추론 서버 (MeloTTS base + tone color converter) — 스텁 교체 완료 (커밋 ec1199b)
- [x] NPC voice_id 매핑 — `voice_map.yaml` 로 관리 (DataTable 대체)
- [x] dialogue_text 흐름 — ActionBatch.Dialogue 액션의 `Parameters["text"]` 를 `_dispatch_npc_audio` 가 직접 사용 (별도 필드 불필요, main.py:368-374)
- [x] emotion → voice 파라미터 매핑 테이블 — voice_map.yaml `npcs.*.emotions` (NPC×emotion → ref/speed), TTSService pre-extract 일괄
- [x] emotion 하드코딩 제거 — `main.py` Dialogue 액션의 FacialState/Parameters.emotion 을 `_dispatch_npc_audio` 로 전달

**→ M2 완료**

#### M3 — 운영 강화
- [ ] TTS 타임아웃·재시도 정책 (현재는 단발 실패 시 자막만)
- [ ] `request_id` 기반 로그 trace (LLM ↔ TTS ↔ UE5 일관성)
- [ ] 동시 발화 1–2 NPC 큐잉 검증
- [ ] LLM 스트리밍 도입 여부 재검토 (지연 측정 결과 보고)

#### M4 (선택) — 표정·제스처 동기화
- [ ] MetaHuman Lip Sync 또는 Audio2Face 에 오디오 스트림 분기
- [ ] AnimBP 에 `LipSyncCurve` / `FacialExprParams` / `GesturePose` 입력 추가
- [ ] `animation_metadata.emotion` → AnimBP `EmotionMood` 파라미터 주입
- [ ] (별도 브랜치 R&D) StreamingTalker / Audio2Gesture / CAP4D

---

### 추가 음성 입력 (ASR) — 본 단계는 TTS 안정화 후
- [ ] `asr-service` (Whisper 계열 WS 스트리밍) — 부분/최종 결과 이벤트
- [ ] UE Mic Capture 컴포넌트 — PCM/Opus → asr-service
- [ ] ASR 결과 → NPCManager → LLM 전달
- [ ] (전환 완료 후) ChatWidget 채팅 기록 코드 제거 또는 레거시 모드로 분리

---

### BehaviorTree → StateTree 마이그레이션 (잔여)
- [ ] 에디터: `ST_NPC.uasset` Transition 조건(`bHasAction==true`) 및 Evaluator 연결 확인
- [ ] `BB_NPC.uasset` 에서 HasAction / SubAction / BehaviorMode / FacialState 키 제거 (ST 에셋 확인 후)

### DA_NPC_Actions 몽타주 등록 (에디터 작업)
- [ ] `Block` / `Dodge` / `SitDown` / `SitUp` / `LieDown` / `LieUp`
- [ ] `Give` / `PickUp` / `Drop` / `Eat` / `Comfort` / `Craft` / `Repair` / `Pray` / `Read`
- [ ] `Emote_*` / `Dance_*` / `Sing_*` — LLM 스타일 키 (대표값 사전 등록)

---

### 알려진 제약
- WebSocket ws:// 연결: Quest 와 PC 가 같은 LAN 필요 (인터넷 라우팅 없이)
- ChatWidget 텍스트 입력: Quest 가상 키보드 API 별도 통합 필요 (Phase 4)
- 손 메시 애니메이션: MetaXR HandPoseRecognition 연동 별도 작업
- SyncCapsuleToHMD: Z축 처리는 계단/경사로 등 지형에 따라 튜닝 필요
- `bPackageForMetaQuest=True` 가 Meta 전용 매니페스트를 추가 — Quest 외 안드로이드 기기에는 설치 안 될 수 있음

---

## Done

- [x] LLM 단순 대화 경로 — `UNPCManager::SendPlayerDialogue`(snake_case PromptPayload→BuildPrompt), VRPlayerCharacter/VRPawn `SendNPCDialogue` exec, HandleNPCDialogue 화면 자막. Python 파이프라인 기완비라 UE5 입력측만 연결 (커밋 50add82) — 2026-05-30
- [x] NPC 상태 소유권 단일화 + BehaviorMode orphan 버그 수정 — CurrentBehaviorMode 미저장으로 Combat 자동 Attack 죽어있던 것 수정(ExecuteActionBatch가 Batch.Mode 저장), BehaviorMode를 NPCStateComponent 소유로 이동, CurrentActionType 3중 표현 제거(ActionComp 단일 소스) (커밋 bc16134) — 2026-05-30
- [x] NPC 액션 파이프라인 비동기 완료 모델 + 중복제거 리팩토링 — 동기 완료를 bIsBusy 폴링 비동기로 전환(이동/몽타주 콜백), danger 단일화·EQS gen·GameplayTagUtils·ClearActiveActionState (커밋 0428585·95fc71d) — 2026-05-30
- [x] VR 자세 시스템 Phase 1-3 C++ 구현 — PlayerGameplayTags Posture 태그 3종, EVRPosture·FOnVRPostureChanged, 캘리브레이션(2초 샘플링), 슈미트 트리거 히스테리시스, 동적 캡슐+Rising Floor 역보정, VInterp 스무딩, 자세별 이동속도 — 2026-05-23
- [x] 메시 폴더 구조 개편 — X_Bot → Content/Core/Mesh/NPC/, 손 메시 → Content/Core/Mesh/Player/ — 2026-05-25
- [x] TTS M2 완료 — Dialogue.FacialState → TTS emotion 매핑까지 연결, 감정별 voice ref/speed 가 실제 합성에 반영 — 2026-05-19
- [x] TTS voice_map emotion 스키마 — NPC × emotion(EFacialState 9종) → ref/speed, base_voices/ 폴더 분리, lifespan pre-extract 일괄 — 2026-05-19
- [x] TTS Server.bat 통합 — LLM(8000) 포그라운드 + TTS(8001) 백그라운드 별도 창 — 2026-05-19
- [x] TTS M2 — OpenVoice v2 통합 (MeloTTS base + tone color converter, NPC별 음색, voice_map.yaml) — 2026-05-19
- [x] TTS M1 검증 — NPCAudioStreamComponent 첨부 + TTSService 기동 + 실 발화 확인 — 2026-05-19
- [x] SLM pre-warm — 서버 startup 시 dummy raw 호출로 모델 메모리 로드. 첫 location_decision cold-start 4.7s 제거 — 2026-05-16
- [x] logger basicConfig 추가 — `logger.info` 가 콘솔에 안 찍히던 문제 (handler 없음) — 2026-05-16
- [x] TacticalQueryCooldown 2→6초 + Abort 시 cooldown 시작 — hearing 폭주로 매번 abort 되는 루프 차단 — 2026-05-16
- [x] location_decision thinking 우회 — httpx raw + few-shot, 2200ms/289토큰 → 400ms/4토큰 — 2026-05-16
- [x] Stale 패킷 임계값 2→10초 — SLM thinking 호출이 큐 막는 시간 대응 — 2026-05-16
- [x] TacticalLLMTimeout 8→20초 + Python LLM 호출 시간 로그 추가 — gemma thinking 시간 대응 — 2026-05-16
- [x] Stale EQS 응답 차단 — TacticalQueryGeneration 카운터 + Python echo (request_gen) 매칭 — 2026-05-16
- [x] location_decision LLM 빈 응답 수정 — num_predict 20→300 (gemma e4b thinking 모델 대응) + 1회 재시도/진단 로그 — 2026-05-16
- [x] 자동 Track 주입 제거 — STTask_PrepareNextAction 비전투 블록 삭제, Combat Attack 자동 주입만 유지 — 2026-05-16
- [x] TTS dispatch 폴백 정리 — Python 임의 NPC 폴백 제거, target_npc 없으면 skip — 2026-05-16
- [x] NPCAudioStreamComponent 지연 측정 로그(T0/T1/T2) 추가 — 2026-05-16
- [x] TTS 통합 M1 스텁 (Python TTSService + Orchestrator dispatch + UE5 NPCAudioStreamComponent) — 2026-05-13
- [x] Meta Quest 3S 포팅 Phase 2 (.ini 설정 분량) — DefaultEngine.ini AndroidRuntimeSettings·OpenXR·Mobile RendererSettings — 2026-05-13
- [x] Meta Quest 3S 포팅 Phase 1 일괄 — OpenXR 플러그인·VRPawn·BP_VRPawn·IMC_VR·IA 6종·PC/VR 입력 분리·GameMode DefaultPawn 설정·WristWidget·OpenXR 키 매핑 — 2026-05-08~09
- [x] BT→StateTree 마이그레이션 C++ 데드코드 제거 (Key_HasAction/SubAction/BehaviorMode/FacialState 쓰기 삭제) — 2026-05-08

## Handoff Notes

- **Quest 빌드 보류 (2026-05-16)**: USB-C 케이블 미보유로 실기 사이드로딩 불가. **현재 코드는 다 준비됨** — VR Phase 1·2 (OpenXR·VRPawn·BP_VRPawn·Android 패키지 설정·OBB 통합·Vulkan·arm64) 모두 Develop 머지 완료. 케이블 도착 즉시 위 "Quest 빌드" 체크리스트 실행하면 됨. **잊지 말 것**: ① 케이블 도착 알림 / ② Android SDK 머신 설정은 .ini에 없으니 새로 셋업 필요 / ③ Quest 와 PC 같은 Wi-Fi 확인.

- **gemma thinking 우회 패턴 (2026-05-16)**: gemma3/4 instruct 모델은 chat template 안에 thinking(reasoning) 토큰 생성이 강제됨. langchain `ChatOllama.invoke` 로 호출하면 항상 chat template 적용 → 단답 prompt 도 200+ thinking 토큰. **해결**: `httpx` 로 Ollama `/api/generate` 직접 호출 + `raw=true` (chat template 우회) + few-shot prompt (Answer: 까지 채워주면 모델은 다음 한 단어만 생성). **측정 비교**: 기존 2200ms/289토큰 → 새 400ms/4토큰. **How to apply**: ① `num_predict` 작게(10) + `stop=["\n"]` 로 안전 마진. ② raw 모드는 instruction 튜닝 효과 약화 — 단답형/분류 작업에만 사용, 자유 대화는 chat template 유지. ③ keep_alive="5m" 로 모델 메모리 유지(cold-start 회피). ④ `request_timeout` 20초로 cold-start 6초 마진.
- **EQS request_gen 프로토콜 (2026-05-16)**: location_decision payload 에 `request_gen` (UE5 → Python echo → UE5) 필드 추가. UE5 `TacticalQueryGeneration` 은 `StartTacticalQuery` / `AbortTacticalQuery` 마다 증가, `NotifyLocationDecisionReady` 가 호출되면 echo 받은 값과 비교해 stale 응답 무시. **Why**: 후보 ID 가 `SAFE`/`OPTIMAL`/`AGGRESSIVE` 3개 카테고리 고정이라 신/구 응답을 ID 만으로는 구분 불가 — 옛 응답이 우연히 새 CandidateMap 의 같은 ID 와 매칭되어 잘못된 좌표를 사용할 위험 있었음. **How to apply**: ① `request_gen=0` 은 stale 검사 우회(레거시 메시지 호환). ② Python 은 단순 echo — 검증/저장 책임 없음. ③ 다른 비동기 응답형 메시지(예: TTS request_id)에서도 같은 패턴 재사용 권장.
- **gemma SLM thinking 토큰 주의 (2026-05-16)**: `gemma4:e4b` (location_decision SLM)는 답 토큰 전에 사고(reasoning) 토큰을 흘리는 thinking 모델. `num_predict=20` 으로 호출하면 추론 중간에 잘려 `response=""` + `done_reason="length"` 로 떨어짐 (Ollama 직접 호출로 검증). **How to apply**: ① 단답형 응답이라도 num_predict 200+ 확보. ② 새 LLM 호출 추가 시 모델별 thinking 여부 확인 필수 — gemma3/4 e/p 시리즈는 thinking 활성. ③ 만약 사고 토큰 자체가 비용 부담이면 `/no_think` 같은 시스템 프롬프트 또는 chat template 강제(`<start_of_turn>model\nANSWER:` 처럼 직접 답 유도) 검토. ④ 재시도 로직은 동시성/네트워크 결함에만 의미가 있으므로 유지하되 num_predict 가 1차 진단 포인트.
- **자동 Track 주입 제거 (2026-05-16)**: `STTask_PrepareNextAction.cpp` 비전투 자동 Track 블록 삭제. **Why**: BB.TargetActor 가 채워지면 ST 가 매 tick "할 일 없네? Track 주입" → 0.5s 타이머가 MoveToActor 반복 → NPC 가 플레이어를 무한 따라다님. 거리/시야 이탈 해제 로직도 없었음. **How to apply**: ① 비전투에서 NPC 가 시야에 들어와도 가만히 있는 게 정상. 추적이 필요하면 LLM 이 Follow/Track 액션을 명시해야 함. ② Combat 모드의 Attack 자동 주입은 유지(전투 반응성). ③ "NPC 가 너무 정적"이라는 피드백 오면 LLM 프롬프트 / 또는 거리·시야 조건부 Track 재도입 검토.
- **TTS dispatch 정책 (2026-05-16)**: `main.py::_handle_prompt` 의 `"NPC_Debug"`/`"Skadi"` 하드코딩 폴백 제거. **Why**: Python 은 UE5 NPCMap 상태를 모름 — 임의 폴백은 잘못된 NPC 가 발화할 위험. **How to apply**: ① 우선순위는 `final_action.AgentID` → `target_npc_from_payload`. 둘 다 없으면 dispatch skip + `[Main][TTS] target_npc 미지정` 로그. ② `target_npc_id` 는 VRPawn.cpp:299 perception 결과를 ChatWidget 이 Envelope 에 실어 전송. NPC 안 보고 prompt 보내면 발화 대상 없음이 정상. ③ M2(LLM 이 dialogue_text 산출) 단계에서 `final_action.AgentID` 가 항상 채워지면 자연 해결.
- **NPCAudioStreamComponent 지연 측정 로그 (2026-05-16)**: `[NPCAudio][T0]` PlayFromUrl → `[T1]` WS Connected +ms → `[T2]` 첫 청크 → Play() +ms. M2 본체 도입 후 첫 음 지연 목표 < 600ms 검증용. 헤더에 `HandleConnected()`, `PlayRequestedAt` 추가.
- **Quest Phase 2 .ini 결정 사항 (2026-05-13)**: Package=`com.jukanmi.ue5mcpvr`, Min SDK 32 / Target 34, ASTC 단일 텍스처 포맷, OBB 통합(`bPackageDataInsideApk=True`), Vulkan + arm64 단독, `bPackageForMetaQuest=True`. **Why**: Quest 3S 단독 타깃 가정. Min 29/Target 32(메모 원안)는 Quest 2/Pro 폴백용 — 현재 호환 요건 없음. **How to apply**: ① NDK r25c·JDK 17·Android SDK 32·34 설치는 머신별이라 .ini 에 없음, UE Editor → Project Settings → Android SDK 에서 직접 지정. ② `bPackageForMetaQuest=True` 가 일부 Meta 전용 매니페스트를 추가 — Quest 외 안드로이드 기기에는 설치 안 될 수 있음. ③ ASTC 단일이면 PC 에디터에서 모바일 프리뷰 시 텍스처가 느리게 빌드될 수 있음, 평소 작업은 Desktop 프리뷰로.
- **TTS M1 스텁 구조 (2026-05-13)**: TTS 흐름은 `명세.md` §5.3 + `vibevoice-dev-spec.md` + `docs/TTS_Integration_Plan.md` 3개 문서가 정본. 본 단계는 **사인파 더미 서버 + dialogue_text 고정 문자열** 로 end-to-end 파이프라인만 깐 상태. **Why**: VibeVoice 본체(M2) 도입 전에 Envelope·디스패쳐·USoundWaveProcedural 경로의 정합성을 먼저 검증하기 위함. **How to apply**: ① UE5 에디터에서 NPC BP(예: BP_SmartNPC)에 `NPCAudioStreamComponent` 를 수동 첨부해야 재생됨. ② TTSService 는 별도 프로세스(`python -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001`). ③ LLM 응답 스키마는 아직 dialogue_text 미포함 — M2 단계에서 LLM 출력에 추가하면 main.py `_dispatch_npc_audio` 의 더미 문자열 대체. ④ voice_id 는 임시로 `voice_<npc_id>` 포맷, DataTable 매핑은 M2.
- **PC/VR 입력 에셋 폴더 분리 (2026-05-09)**: `Content/Core/Input/` 루트의 IA_*.uasset 7종을 `Computer/` 하위로 이동했고, VR용은 `VR/` 폴더에 신규 생성. **Why**: 데스크톱 빌드와 Quest 빌드가 다른 IMC를 쓰는데 파일명이 충돌하면 IMC가 잘못된 IA를 참조할 위험. **How to apply**: 기존 `BP_Player`/`BP_VRPawn`에서 IA 참조 경로가 깨졌을 수 있음 — 두 Pawn BP 컴파일 후 IA 레퍼런스 누락 경고 확인 필수. 새 IA 추가 시 PC/VR 어느 쪽에 속하는지 폴더 결정 후 생성할 것.
- **EQS Named Parameter 바인딩 위치**: Generator의 Search Radius는 UE5 EQS 에디터에서 Named Parameter로 바인딩 불가. **Test 노드의 Score Factor**에만 바인딩 가능. C++의 `SetFloatParam("DistanceWeightParam"/"CoverWeightParam")`은 Test 가중치에서만 의미가 있음. Generator 반경은 에셋에 고정값으로 둘 것.
- **EQS 에디터 작업 미완 (빌드 후 필수)**: DistanceWeightParam/CoverWeightParam → Test Score Factor, SafeDistance → Distance Filter Min, AggressionWeightParam → TacticalPositionsQuery Inverse Distance Test에 Named Parameter 바인딩 필요.
- **Blueprint CDO PerceptionTickInterval 리셋 필요**: C++ 생성자에 9.0f 추가했으나, 이미 저장된 BP CDO가 3.0f를 직렬화하고 있음. 에디터에서 NPC Blueprint → Details → PerceptionTickInterval 옆 ↺(Reset) 버튼 클릭 후 저장/재컴파일 필요.
- **LLM 모델 라우팅**: importance=normal→gemma4:e4b, high→gemma4:12b, core→gemma4:26b. 모델 변경 시 `dialogue.py`/`main.py`/`debug.html` 세 곳을 동시에 맞출 것.
- **NPC 시작 시 따라오는 버그**: ① `OnTargetPerceptionUpdated` — 중립 대상(FinalDanger<0.5)은 전술 쿼리 미발동. ② `STTask_PrepareNextAction` — Combat 모드일 때만 전술 쿼리 발동. Python DB 호감도 잔류 시 디버그 대시보드(http://127.0.0.1:8000/debug)에서 초기화 필요.
- **Track 몽타주 없음**: `ExecuteTrack`은 이동(MoveToActor 0.5s 반복)만 사용. DA_NPC_Actions "Track" 키 등록 불필요.
- **code-review-graph 오탐 패턴**: anonymous namespace 함수(EvalSafeScore 등), FastAPI 라우트, LangGraph 노드, C++ 매크로(UE5_MCP_VR_API)는 dead_code로 잘못 잡힘. 제거 전 반드시 grep으로 직접 호출 사이트 확인할 것.
- **JSON 키 폴백 제거 배경**: Python interface_output.py가 snake_case로 통일 완료됨. 혹시 Python 쪽에서 키가 안 오는 경우, MCPJsonUtils에 폴백을 추가하지 말고 Python 출력 스키마(actions.py)를 먼저 확인할 것.
