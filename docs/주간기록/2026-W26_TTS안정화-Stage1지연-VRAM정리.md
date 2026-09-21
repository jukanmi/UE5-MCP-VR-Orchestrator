# W26 (2026-06-23 ~ 06-29) — TTS 안정화·Stage1 지연·VRAM 정리·VR 실감형 전투(§3/§4/§5)·리팩토링

<!-- 06-23 집중 디버깅 세션. PR #11(TTS CosyVoice2 마이그레이션, 06-16) 후속 실기동 문제 해결 + 인지엔진 지연·VRAM 동시 정비. -->

## 핵심
PR #11 로 옮긴 CosyVoice2 WSL2 TTS 가 실기동에서 줄줄이 깨지던 것을 잡고, 같은 16GB VRAM 예산 안에 LLM·TTS·ASR 을 욱여넣으며 인지엔진 지연도 줄였다. ① **TTS 기동 3종 버그**: Windows .venv 로 CosyVoice import 시도(WSL 전용) → BAT 의 cmd.exe `start ... wsl bash -c "...$()...괄호..."` 인라인이 괄호를 mangle 해 `SITE` 빈 값 → `LD_LIBRARY_PATH` 깨짐 → onnxruntime CUDA provider 가 cudnn 못 찾아 flow.decoder CPU 폴백. 전부 `run_tts_wsl.sh` 분리로 해소. ② **gemma4-12b 별칭 부재** — 12B 호출 전부 조용한 500(Fast-Path 폴백돼 NPC 는 안 멈추나 품질 저하). ③ **Stage1 e4b 구조화 지연** — langchain `with_structured_output`(3506ms·분산 1.2~8s) 가 범인, Ollama format 직접 호출로 1518ms(2.3배·안정). ④ **VRAM 정리** — ASR CPU 전환·whisper large-v3/medium 캐시 삭제(~4.3GB), 재계획 빈도 절반(ReplanTurnLimit 5→10). KV 양자화는 이 GPU 에서 FA 커널 크래시로 불가 확인. ⑤ **사후 롤백**: 안정화 끝낸 CosyVoice2 가 결국 VRAM 메모리 과다로 판단돼 마이그레이션 전 MeloTTS+OpenVoice 로 복귀(`1d560e8`) — TTS 기동 작업은 git 이력에만 보존.

그리고 06-26~27 에 **VR 실감형 전투(§3/§4/§5)** 를 별도로 구현: 히트스캔 즉발 전투를 물리 기반으로 전환. ⑥ 손 스윙 운동에너지(½mv²)·투사체 근접/원거리 데미지(`d5ee80b`), Hit 본→부위 배율(머리 2.0/사지 0.75)+VR 햅틱(`165ee32`·`75eac60`), 사망 패시브 래그돌(`8cbad54`), 트리거형 액티브 래그돌(약타 Flinch/강타 넉다운→기상, Gemini 5R `e2da127`). ⑦ 그 위에 6/12+ 누적 중복을 behavior-preserving 으로 정리한 **리팩토링 라운드**(KineticDamage·인지엔진·TTS/ASR·플레이어폰 — PR #15). VR 전투는 C++ 빌드 통과, 게임플레이 PIE·에셋 검증(SPEC §7) 대기.

## 추가 작업 (06-25) — NPC 자율성·재계획·인벤토리

### 재계획 트리거 개편 (시간기반 → 이벤트기반)
- **plan 달성 신호**: e4b Stage1 `DialogueResponse.plan_achieved` 필드 추가 → `ModeActionRequest.PlanAchieved`(npc_id→bool) 회신 → UE5 `FlagPlanAchieved()` → 다음 턴 강제 재계획(새 plan 생성). e4b 가 plan goal 완수 판단 시만 12B 재가동.
- **combat 한정 replan**: `SmartNPCAIController` 두 FlagDangerReplan 호출(시야 최초·perception tick) 모두 `GetBehaviorMode() != Combat` 가드. 이미 Combat 중 연속 perception 은 재계획 안 침(plan 폭주 방지). 첫 전투 전환만 트리거.
- **시간기반 제거**: `TurnsSinceReplan`·`ReplanTurnLimit`·`IncrementReplanTurn` 삭제. `ShouldReplan() = !bIsValid || bDangerReplanPending || bPlanAchievedPending`. plan 있으면 e4b 단독 무한 유지 — 불필요한 12B 호출 근절.
- **plan regex 호환**: 12B 실출력이 명세(`goal=.. | steps=s1;s2`)와 달리 `[Plan: Goal: .. ; Steps: s1, s2]` 형식 → regex 에 `Goal:`/`Steps:` 키워드·`;`/`,` 구분자 모두 수용. (이전엔 전 응답 "plan 라인 누락"으로 plan 미저장)

### NPC 인벤토리 동적 전송
- **UE5 → Python 매 prompt 동봉**: `NPCManager` 가 `NPCInventoryComponent::GetInventoryJson()` 파싱해 `npc_inventory: {npc_id: [items]}` payload 추가. yaml 정적 대신 **실시간**(아이템 획득/소모 즉시 반영). `GetInventoryJson()` 은 기존 존재했으나 미호출 데드 — 이제 배선.
- **Stage1 LLM 주입**: `DIALOGUE_STRUCTURED_PROMPT`·fallback 에 `{inventory}` — "보유 아이템만 GiveItem/HandObject, 없는 건 거절". 근거 없는 give 액션 차단.
- **스키마 체인**: `PromptPayload.npc_inventory` → `GesPrompt.npc_inventory` → dialogue Stage1 npc_id별 조회.

### 브라우저 테스트 채팅 페이지 (`/test_chat`)
- UE5·TTS·음성 없이 대화 파이프라인 텍스트 검증. `GET /test_chat` 인라인 HTML(다크). 기존 `/api/debug/prompt` 재사용.
- 표시: NPC 대사·Mode·Facial·액션 목록·Plan(goal/steps 토글)·plan 달성 뱃지. 우측 패널: NPC별 모의 인벤토리 편집("이름 x수량") → 전송 동봉.
- Follow 미발동 완화: Stage1 프롬프트에 명령→액션 few-shot("따라와"→Follow) 추가. ⚠️ 4B 한계로 결정론 아님 — 잦으면 Rules 보정/모델 격상 검토(미적용).

## 주요 작업

### TTS CosyVoice2 실기동 안정화
- **WSL 런처 스크립트 분리** (`beb7d94`): cmd.exe `start` 인라인의 괄호 mangle(→`SITE` 공백→onnx CPU 폴백) 회피. fragile bash(`$()`·괄호·작은따옴표·`&&`)를 `run_tts_wsl.sh` 안에 가두고 `Server.bat`·`TTSServer.bat` 은 `wsl bash <script>` 한 줄만. `.gitattributes` `*.sh eol=lf`(CRLF 면 bash `\r` 에러). 프로브 BAT 으로 SITE·LD·cudnn 정상 검증.
- **voice_map ref_text 인라인화** (`7cd24bf`): 구 최상위 `ref_texts:` 섹션 제거, emotion entry 마다 `ref_text:` 직접 기입. `voice_resolver` inline 우선·없으면 최상위 폴백(하위호환). Moca 9 emotion 독립 WAV+대본(`docs/음성작업 최종본/`). ⚠️ CosyVoice2 는 `len(tts_text)<len(prompt_text)` 시 "too short" 경고 — 짧은 테스트 문장에서만, 실발화 무관. ref WAV 는 숫자·전문용어 없는 5~8s(숫자는 `CV_TEXT_FRONTEND` 가 영어 정규화해 오디오 불일치).
- **⚠️ CosyVoice2 롤백 — MeloTTS+OpenVoice 복귀** (`1d560e8`): 위 TTS 기동 안정화 후 실구동했으나 CosyVoice2 WSL2 가 16GB VRAM 예산에서 **메모리 과다**(12B 8GB+e4b 3GB+CosyVoice GPU+UE5 PIE 동시 OOM 압박). 성능(음색 다양성·zero-shot 클로닝) 일부 포기하고 마이그레이션 직전(`ae7a6f5`) MeloTTS+OpenVoice Windows .venv 구성으로 forward-fix 복원. `git revert` 대신 `git checkout ae7a6f5 -- <TTSService 4파일·TTSServer.bat>` — 마이그레이션 후속 `11e7619` 에 NPCMap/HUD 비-TTS 수정이 섞여 revert 가 그걸 같이 되돌리기 때문. `ref_transcriber.py`(마이그레이션 신설)·`run_tts_wsl.sh` 삭제, `Server.bat` TTS 줄 Windows .venv 복귀·WSL_REPO wslpath 제거. ASR CPU(small/int8)·Stage1·Replan 최적화는 WSL 무관이라 유지. ➡️ `2dda9a0`·`beb7d94`·`7cd24bf` 의 TTS 코드 작업은 실효 소멸(향후 VRAM 여유 시 CosyVoice 재도입 대비 git 이력엔 보존).
- **gemma4-12b 별칭 복구**: `ollama cp hf.co/mradermacher/Gemma-4-12B-OBLITERATED-GGUF:Q4_K_M gemma4-12b`. 부재 시 Stage2 정제·supervisor 12B 호출 전부 "model not found" 500(에러가 12B 언급 안 해 진단 어려움 — 별칭부터 의심).

### Stage1 e4b 구조화 지연 최적화
- **직접 format 호출** (`0f41769`): 실측 — langchain `with_structured_output` warm avg 3506ms(분산 1.2~8s) vs Ollama `/api/chat` format=schema 직접 1518ms(안정) vs 자유텍스트 823ms(액션 누락 위험). 범인은 grammar 디코딩이 아니라 langchain 래퍼 오버헤드. `llm_factory.ollama_structured()` 신설, dialogue Stage1 교체. 필드 required grammar 강제·`_serialize_dialogue`·다운스트림 무변경. ⚠️ ruff 가 함수 추가 전 첫 edit 에서 미사용 import(httpx/Type/BaseModel) 스트립 — 함수 본문 추가 후 재삽입 필요했음.

### VRAM·재계획 정리
- **ASR CPU 전환** (`beb7d94`): `Server.bat` env `WHISPER_DEVICE=cpu`/`WHISPER_COMPUTE=int8`/`WHISPER_MODEL_SIZE=small`. large-v3(GPU)→small(CPU). HF 캐시 large-v3(2.88GB)·medium(1.43GB) 삭제. ASR 은 push-to-talk 단발이라 CPU small 로 충분(~0.5s).
- **재계획 빈도↓·데드코드 제거** (`e48b7e0`): `ReplanTurnLimit` 5→10 — warm(e4b 단독) 구간 2배로 12B 재계획 절반. `ReplanDangerThreshold`+`ShouldReplan(float)` danger 분기·파라미터 제거(유일 호출처가 0.0 전달이라 무효한 데드코드). danger 재계획은 `FlagDangerReplan`→`bDangerReplanPending` 경로 유지.

## 메모 (Memo.md Handoff Notes, 2026-06-23)
- **TTS 기동은 `run_tts_wsl.sh` 경유 필수**: cmd.exe `start` 인라인 괄호 mangle 재발 방지. `.sh` LF 강제.
- **Ollama KV 양자화 금지(이 환경)**: `OLLAMA_FLASH_ATTENTION=1`+`OLLAMA_KV_CACHE_TYPE=q8_0/q4_0` → 모델 로드 즉시 `CUDA error: shared object initialization failed`+buffer overrun 크래시(e4b·12B 공통). VRAM 절약 목적이었으나 불가. `HKCU:\Environment` 제거 후 Ollama 완전 재시작.
- **VRAM 16GB OOM 잠재(미해결)**: 12B(8GB)+e4b(3GB)+CosyVoice2 GPU(2GB)+UE5 PIE 동시 → 부하 시 Ollama 500/ReadTimeout. `_handle_location_decision` except 가 Fast-Path 폴백해 NPC 는 안 멈춤. 더 잡으려면 CosyVoice CPU 전환(`CV_USE_GPU=0`)·`num_ctx` 축소. KV 양자화는 위 크래시로 불가.
- **`ReplanDangerThreshold` 데드였던 이유**: `ShouldReplan` 유일 호출처 `NPCManager.cpp` 가 대화 경로(실시간 perception danger 없음)라 0.0 전달. 실제 danger 트리거는 perception 경로의 `CombatDangerThreshold`(0.5)→`FlagDangerReplan` 플래그.

## 검증 대기
- TTS: `Server.bat` 기동 후 MeloTTS+OpenVoice 8001 정상·메모리 체감 확인(.venv 에 deps 잔존 여부)
- WSL `cosyvoice-venv`·모델 디렉터리 수동 삭제로 디스크 회수
- Stage1 직접 format: UE5 PIE 실대화에서 액션 실행·지연 체감
- ReplanTurnLimit 10: C++ 빌드 후 12B 재계획 빈도 로그 확인
- 2 NPC 동시 발화 TTS 재생(`NPCAudioStreamComponent` ×2 PIE)

## 추가 작업 (06-26~27) — VR 실감형 물리 상호작용 §3/§4/§5
논문 `docs/VR 실감형 상호작용 시스템.pdf` 기반. 히트스캔 즉발 전투를 물리 기반(운동에너지·부위·래그돌)으로 전환. SPEC: `docs/SPEC_kinetic_damage.md`·`docs/SPEC_active_ragdoll.md`. ⚠️ C++ 빌드는 Develop 머지로 통과, **게임플레이 PIE·에셋(Physics Asset·기상 몽타주·BP 프로퍼티) 검증(SPEC §7)은 대기**.

### §5 부위 인지·햅틱·패시브 래그돌 (06-26)
- **부위 인지 데미지** (`165ee32`·`9192e89`): `SmartNPC::BoneToBodyPart` 가 `Hit.BoneName` substring 매칭 → `EBodyPartType`. 머리 2.0·몸통 1.0·사지 0.75. 사지 좌우 4분할. `EffectiveDamage=Max(0,Damage-Defense)×배율`. 미식별/캡슐히트(None)→Torso 폴백. ⚠️ **Mixamo X_Bot·UE Mannequin 양 네이밍 수용**(좌우=`right`/`left` 접두 또는 `_r`/`_l` 접미) — 구현 전엔 Mannequin 전용이라 X_Bot 스켈레톤선 전부 Torso 폴백·좌우 실패였음(`bfa4f6e`).
- **럼블 햅틱 B+A 폴백** (`165ee32`·`75eac60`): 명중 시 B `PlayHapticEffect`(VR 모션 컨트롤러 정석)+A `ClientPlayForceFeedback`(게임패드). 미할당 no-op. ⚠️ **VR 실기 럼블엔 B 에셋(UHapticFeedbackEffect_Curve) 필수** — A만 할당하면 VR 무음.
- **패시브 래그돌** (`8cbad54`): `HandleDeath` 사망 몽타주 제거 → 메시 전신 물리 시뮬+캡슐 충돌 OFF+이동 정지. 치명타 본+`ShotDirection` 임펄스(`DeathImpulseStrength`, 0=순수 중력). ⚠️ **메시 Physics Asset 필수**(없으면 `SetSimulatePhysics` 조용히 무효 — 시신 안 쓰러짐).

### §4 동역학 데미지 — 스윙 근접·투사체 (06-26, `d5ee80b`)
- **근접**: `VRPawn` 손 컨트롤러 위치델타/dt 로 손 속도 추적(kinematic → `GetVelocity()=0`). 능동 스피어 오버랩(`TryMeleeHits`)에서 `½·WeaponMass·v²·Scale` 데미지. 2단 임계 — `MinImpactSpeed`(밀치기, `LaunchCharacter`만)~`MeleeStrikeSpeed`(강타, `TakeDamage`→공격 인지). **데미지=공격 인지** 단일 기준.
- **원거리**: `OnAttack` 히트스캔 폐기 → `AKineticProjectile` 스폰. 명중 시 투사체 ½mv². `ProjectileClass` BP 지정 필수.
- **데미지 규약**: 근접·투사체 모두 `FPointDamageEvent`(BoneName·ShotDirection) — `SmartNPC::TakeDamage` 가 부위 배율·래그돌 임펄스 결합. BoneName 빔→`FindClosestBone`(Physics Asset 기반) 보강.
- 레벨 멀티 NPC 배치(`70e4f65`). ⚠️ VRPlayerCharacter(레거시 플랫스크린)는 히트스캔 유지 — 활성 폰이 VRPawn 인지 BP GameMode 확인.

### §3 액티브 래그돌 — 트리거형 hit-react (06-27, `e2da127` + Gemini 5R)
- **분기**: `TakeDamage`→`ReactToHit(ActualDamage)`. `>= KnockdownImpulseThreshold`(40) → Knockdown(전신 래그돌→안착→기상), 미만 → Flinch(상체 PD 복귀·Tick 램프). 데미지를 강도 프록시로 사용(FPointDamageEvent 에 임펄스 필드 없음).
- **데미지 상시**: ApplyDamage·인지는 반응 분기와 무관하게 항상. AI(StateTree)만 `PauseAI`(StopLogic — UnPossess 안 씀, BB 보존).
- **기상**: 골반 선속도<임계 지속→`BeginGetUp`(엎/누움 판정·캡슐 재배치·기상 몽타주·블렌드 램프)→`ResumeAI`(루트부터 재평가=기상 후 위협 재판단). 넉다운/기상 중 강타 재호출=재진입(저글).
- **동시 상한** `MaxConcurrentKnockdown`(3): `UNPCManager` 카운터(GameInstanceSubsystem, PIE 세션 리셋 — 구 static 잔존 버그 회피). 초과분 Flinch 폴백.
- **Gemini 5R 반영**(`65b3024`·`cb0471b`·`a19dc4d`·`b46ee36`·`2b45209`): 카운터 static→매니저·안착 본 폴백·바닥트레이스 `ECC_WorldStatic`+`FindTeleportSpot`(끼임)·기상 캡슐 Yaw 정렬·몽타주 EndDelegate+워치독·멜리 저글(오버랩에 `ECC_PhysicsBody` 추가)·VR 손속도 스파이크 클램프·투사체 널가드. PR #13.
- ⚠️ 전제: 메시 **PA_SmartNPC**(전신 바디·컨스트레인트). 기상 몽타주 `GetUpMontage_FaceUp/FaceDown` 미할당 시 즉시 블렌드 폴백(서기 스냅). `bSpikeReactOnHit=false`(BP 오버라이드 함정 — CameraHeightOffset 와 동류).

### VR 셋업 수정 (06-26, `b0822fd`)
- **카메라 높이 함정**: `CameraHeightOffset` 기본 -30 이 카메라 월드Z 오염→`GetCurrentHMDHeight` 역산 오염→캡슐 30cm 단축→'시야 너무 높음'. **0 으로 교정**(BP 오버라이드도 0 필수). 머리 뜨면 `HeadEffectorOffset`/FBIK 에서 교정(카메라 침몰 금지).
- **아바타 1:1 고정**: 키 스케일 기능 제거. 짧게 적용 시 FBIK 가 머리를 HMD 까지 못 늘려 '머리 낮음' 버그만 유발. `CalibratedStandingHeight` 는 자세판정용으로만 유지.

## 추가 작업 (06-27) — 코드 리팩토링 라운드 (behavior-preserving)
6/12+ 커밋이 쌓이며 생긴 중복을 **출력 불변** 전제로 정리. C++ 는 빌드 후, Python 은 골든 회귀(파서·직렬화 핵심 함수 전/후 출력 100% 동일)로 검증. 스코프·방식은 분기마다 `AskUserQuestion` 으로 확정.

### VR 전투/래그돌 C++ (`6a8e3a9`)
- **`KineticDamage` 공용 헬퍼 신설** — `VRPawn::TryMeleeHits`(근접)·`KineticProjectile::OnHit`(투사체)의 ½mv² 데미지 산출 + `FindClosestBone`·`FPointDamageEvent` 적용 중복을 `KineticDamage::Compute`/`ApplyToNPC` 로 단일화(§4 동역학 단일 규약). 투사체 미사용 include 2개 정리.
- **`SmartNPC` 물리블렌드 헬퍼** — PD 빌드(`MakeOrientationPD`)·전신 시뮬 정지(`StopBodySimulation`) 파일 static 추출(Flinch/기상 공용).
- **[SPIKE] 코드 제거** — `SpikeHitReact`/`SpikeRecover`+프로퍼티 6개+`bSpikeReactOnHit` 분기 삭제(제품 경로 `ReactToHit` 가 대체, throwaway 명시됨). `PhysicalAnim` 은 Flinch 가 써 유지·`MCP|Spike`→`MCP|Ragdoll` 제품화.

### Python 인지엔진·TTS/ASR (PR `refactor/python-dedup`)
- **TTS/ASR** (`caa0a56`): WS 송신 보일러플레이트 `_send_error`/`_send_audio_chunk`(TTS)·`_send_error`(ASR). TTS lifespan·`/api/reload_voices` 의 target SE 추출 루프를 `_pre_extract_target_ses` 로 통합 — `list_voices()` 가 이미 unique ref 산출이라 호출부 `if ref in extracted` 중복가드 제거.
- **main.py** (`0f921d7`): `_handle_slm_reflex`·`_handle_location_decision` 의 Ollama raw 호출 동일 블록을 `_ollama_raw_generate` 로. `location_decision_result` JSON 2곳을 `_location_decision_result` 로. 폴백 audio info `_empty_audio_info`. **§5 envelope 라우팅/타입분기 불침범** — 응답·LLM호출 보일러플레이트만.
- **interface_output.py** (`0f921d7`): `VALID_MODES`/`VALID_FACIALS`·`EMOTION_MAP`·`KEYWORD_ACTION_MAP` 매 호출 재생성 → 모듈 상수 호이스트. `_regex_fallback_parse` 의 "bare→Dialogue" 동일 2블록 → `_strip_to_dialogue`.
- **dialogue.py** (`0f921d7`): `_dialogue_single`·`dialogue_node` 의 `player_id` 추출 동일 2블록 → `_vr_player_id`.
- **검증 방법**: 파서·직렬화 함수를 대표 입력으로 골든 캡처 후 전/후 `diff` — interface_output·dialogue 모두 출력 100% 동일. `_vr_player_id` 는 dict/빈dict/None/객체 등가 단언. import 스모크(torch/langgraph 스택 로드)까지 통과.

### 플레이어 폰·NPCManager C++ (`3f91b97`, 빌드·PIE 검증 완료)
- **`UNPCManager::Get(const UObject* WorldContext)` 정적 헬퍼**: `if(GI=GetGameInstance()) if(GI->GetSubsystem<UNPCManager>())` 이중중첩 idiom 일원화. `GEngine->GetWorldFromContextObject` 로 액터·컴포넌트·월드 어디서든 동일 획득. SmartNPC(register/unregister/GetNPCManager)·VRPlayerCharacter TestPlanHUD·STTask 치환.
- **`PlayerInteractionUtils`(신규 Core/)**: `FindNearestNPCId`(반경 내 최근접 SmartNPC)·`SendDialogueToNpc`(UNPCManager::Get+SendPlayerDialogue). VRPawn·VRPlayerCharacter 의 `DetectNearbyNPC`·`SendNPCDialogue`·`HandleVoiceTranscript` 복붙(~20줄×2 + Manager 페치) 통합. **공유 유틸 방식 채택**(베이스클래스는 BP `.uasset` 컴포넌트 오버라이드 리스크 §9 — 보류).
- **후속 마무리** (`db2f1b7`): NPCActionComponent(location_decision)·NPCStateComponent(emergency_report) 도 `Get` 전환 완료 — 깊은 중첩은 두 else 모두 `Idle` 폴백이라 병합 안전, GI-null 비대칭은 subsystem 이 GI 동반생성이라 `Get`-null ⟺ GI-null 로 등가. VoiceInputComponent `AudioCapture` 정지 3줄(StopTalking·EndPlay) → `StopAudioCaptureStream`. PlayerHUDWidget(무-폰 0 반환 리스크)·InventoryComponent(`UnequipItemByID`→`UnequipItem` 위임 분해됨) 은 안전 dedup 없어 클린 판정. ➡️ **백로그 dedup 전 항목 완료**.
- **C++ 빌드·PIE 검증 완료 (2026-06-27)** — 작성 시점엔 컴파일 불가라 grep 정합성(`GetSubsystem<UNPCManager>` 헬퍼 본문 1곳만 잔존·orphan 변수 0·헬퍼 배선)만 확인했고, 이후 사용자 빌드·PIE 로 통과 확인.
- **랜딩**: PR #14(Python 단독) 닫고 PR #15(Python+C++ 통합, 4커밋)→Develop 재생성.
- **보류(설계 결정, dedup 아님)**: VRPawn↔VRPlayerCharacter death/respawn 통합 — 커서·로그·IsAlive 가드 차이라 공유 베이스클래스 필요(§9 BP 컴포넌트 오버라이드 리스크). util-extract 범위 밖이라 별도 결정 시 진행.

## 커밋
| 날짜 | 해시 | 메시지 |
|------|------|--------|
| 06-27 | `db2f1b7` | refactor: 잔여 UNPCManager idiom(NPCAction·NPCState) + Voice 캡처정지 헬퍼 |
| 06-27 | `3f91b97` | refactor: 플레이어 폰 중복·UNPCManager 조회 idiom 정리 — PlayerInteractionUtils·UNPCManager::Get |
| 06-27 | `0f921d7` | refactor: 인지엔진 헬퍼·상수 정리 — main.py·interface_output·dialogue |
| 06-27 | `caa0a56` | refactor: TTS/ASR WS 보일러플레이트·SE 추출 헬퍼 정리 |
| 06-27 | `6a8e3a9` | feat: KineticDamage 추가·VRPawn/SmartNPC 정리 — Core/NPC 리팩토링 |
| 06-27 | `e2da127` (+5R) | feat: 액티브 래그돌 §3 — 트리거형 hit-react + 넉다운/기상 (Gemini `65b3024`·`cb0471b`·`a19dc4d`·`b46ee36`·`2b45209`) |
| 06-26 | `d5ee80b` | feat: VR 동역학 데미지 §4 — 스윙 근접(½mv²)·투사체·밀치기 |
| 06-26 | `8cbad54` | feat: NPC 사망 패시브 래그돌 + 타격 방향 임펄스 (§5) |
| 06-26 | `9192e89` | feat: 부위 인지 사지 4분할 — 오른팔/왼팔/오른다리/왼다리 (§5) |
| 06-26 | `165ee32` | feat: 부위 인지 데미지 + 명중 럼블 햅틱 (§5) |
| 06-26 | `75eac60` | feat: 명중 럼블 VR 햅틱 경로 + 게임패드 폴백 (§5) |
| 06-26 | `bfa4f6e` | fix: 부위 인지 BoneToBodyPart Mixamo·Mannequin 양 네이밍 수용 |
| 06-26 | `b0822fd` | fix(VR): 카메라 높이 오프셋 0 + 아바타 키 스케일 제거 (1:1) |
| 06-26 | `70e4f65` | chore: 레벨 NPC 배치 — 멀티 NPC 테스트용 |
| 06-23 | `1d560e8` | revert: TTS CosyVoice2 WSL2 → MeloTTS+OpenVoice 복귀 — WSL 메모리 과다 |
| 06-23 | `e48b7e0` | refactor: ReplanTurnLimit 10·ShouldReplan danger 데드코드 제거 |
| 06-23 | `0f41769` | perf: Stage1 e4b 구조화 직접 format 호출 — 2.3배 단축 |
| 06-23 | `7cd24bf` | feat: voice_map ref_text 인라인화 — Moca 9 emotion 독립 음성 |
| 06-23 | `beb7d94` | chore: 서비스 런처 정비 — TTS WSL 스크립트 분리·ASR CPU 전환 |
