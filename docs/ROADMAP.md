# UE5_MCP_VR — 통합 로드맵 (TTS 우선 + 향후 전체)

> 작성 2026-05-30. 코드베이스 탐색 기반. 우선 **TTS에서 다음에 할 것**, 이어서 **프로젝트 전체 향후 작업**.
> 범례 — 규모: S(반나절)/M(1~2일)/L(3일+). 우선순위: 🔴필수 / 🟡중요 / 🟢선택. 의존: 선행 조건.
> 코드 포인터는 `파일:라인` 또는 `파일::함수`. 세션 메모는 `docs/Memo.md`, 완료 이력은 `docs/주간기록/`.

---

## 0. 현재 상태 요약 (Baseline)

| 시스템 | 상태 | 비고 |
|--------|------|------|
| **NPC AI (StateTree)** | ✅ 동작 | 비동기 액션 완료 모델, EQS 전술, perception, 상태 소유권 단일화 |
| **대화 (LLM)** | ✅ 동작 | LangGraph(Input→Supervisor→Dialogue→Output→Rules), persona·RAG·memory·affinity |
| **TTS** | ✅ M2 완료 | OpenVoice v2 + MeloTTS, voice_map.yaml, 감정별 ref. `server.py` v0.6.0 |
| **ASR (음성입력)** | ✅ M2 완료 | faster-whisper large-v3, push-to-talk, `VoiceInputComponent` |
| **VR 아바타** | ✅ 완료 | 풀바디 FBIK, 자세, 부드러운 회전. PCVR(Link) 기준 |
| **인벤토리/전투/체크포인트** | ⚙️ 부분 | `Inventory/`, 전투 기본, `Checkpoint` |
| **ChatWidget UI** | ❌ 제거됨 | 화면 자막(AddOnScreenDebugMessage)만. WorldSpace 위젯 미구현 |
| **멀티플레이어** | ❌ 미구현 | VR 자세 동기화 Phase 6 |
| **Quest 스탠드얼론** | ⏸️ 보류 | 현재 PCVR Link. APK/IP외부화는 단독 타겟 확정 시 |

---

## 1. TTS — 다음에 진행할 것 (M3 운영 강화)

TTS 본체(M2)는 완성. 남은 것은 **운영 품질·표현력·견고성**. 아래는 코드 탐색으로 확인한 구체적 갭.

### 1.1 🔴 감정 → 음색 연결 복구 (M3 핵심) — M ★중요·발견사항
> **⚠️ 핵심 발견**: 감정 TTS 기능이 **`feature/TTSExtend` 브랜치에 완성돼 있으나 Develop 에 미머지**.
> Memo "TTS M2 감정 반영 완료" 는 그 미머지 작업을 가리킨 것 — **현재 Develop TTS 는 감정을 전혀 안 씀**.

- **Develop 현실**: `voice_resolver.resolve_voice(npc_id)` 만 존재(감정 인자 없음). `_synthesize_sync(text, voice_id, language)` 에 emotion 미전달. `main.py:380` 도 `emotion="neutral"` 고정. → 음색 항상 중립.
- **TTSExtend 에 이미 있는 것(포팅 대상)**:
  - `voice_resolver.py`: `resolve_voice_meta(npc_id, emotion) → VoiceMeta(ref, lang, speed)`, `normalize_emotion()`(C++ EFacialState 정합), `list_voices()`(부팅 pre-extract).
  - `server.py`: `_synthesize_sync` 가 ref/speed 메타 사용, emotion 인자 수용.
  - `voice_map.yaml`: `npcs.*.emotions.<E>` → ref/speed (예: Skadi/Happy → Skadi_happy, speed 1.05). `base_voices/` 에 감정별 WAV 존재(Skadi_angry/fear/happy/sad 등 — 이미 머지됨).
- **이미 깔린 배관(Develop)**:
  - LLM `[Facial: Angry]` → `interface_output._parse_mode_and_facial` 파싱 → `GameAction.FacialState`(9종) (`interface_output.py:43-81`).
  - `tts_client.synthesize(emotion=)` 인자 수용, `NpcAudioResponse.animation_metadata.emotion` 존재.
- **할 일**: TTSExtend 의 **emotion 부분만 cherry-pick/포팅**(전체 브랜치 머지 금지 — VR/리팩토링 revert됨). ① `voice_resolver.py` 교체 ② `server.py` 의 `_synthesize_sync`+synthesize 가 emotion→ref/speed 사용하도록 ③ `main.py:380` 에서 Dialogue 액션 `act.FacialState` 전달.
- **파일**: `TTSService/voice_resolver.py`, `TTSService/server.py`, `voice_map.yaml`, `main.py:358-384`
- **검증**: 화난 대사 → Skadi_angry ref·speed 반영, 로그 `voice=… emotion=Angry`.
- **선행 점검**: TTSExtend 의 voice_resolver/server 가 현재 OpenVoice v0.6.0 server 구조와 호환되는지(시그니처 차이) 확인 후 포팅.

### 1.2 🟡 TTS 재시도 정책 — S
- **검증**: 타임아웃은 **이미 존재** — `TTS_TIMEOUT_SEC=3.0`(env 오버라이드 가능), `httpx.AsyncClient(timeout=...)` (`tts_client.py:25,55`). 실패 시 `TTSError`→자막 폴백(`main.py:413-415`).
- **실제 갭 = 재시도 없음**: 단발 실패 시 바로 자막. 일시적 네트워크/동시성 결함에 1회 재시도 여지.
- **할 일**: `synthesize` 호출을 1회 재시도(짧은 backoff)로 감싸고, 그래도 실패면 현행 자막 폴백 유지.
- **파일**: `clients/tts_client.py::synthesize` 또는 `main.py::_dispatch_npc_audio`
- **주의**: 재시도는 일시 결함에만 의미 — 영구 실패(모델 미로드 503)는 즉시 폴백(무한 재시도 금지).

### 1.3 🟡 request_id 기반 로그 trace — S
- **갭**: LLM↔TTS↔UE5 를 잇는 일관 추적 ID 부재. 디버깅 시 어느 발화가 어느 요청인지 매칭 어려움.
- **할 일**: prompt msg_id → TTS request_id → UE5 NpcAudioResponse 까지 동일 trace 필드 전파. 로그 포맷 통일(`[trace=...]`).
- **참고 패턴**: Memo Handoff "EQS request_gen 프로토콜" — echo 기반 ID 전파를 그대로 재사용 권장.
- **파일**: `main.py`, `clients/tts_client.py`, `schemas/npc_audio.py`, UE `NPCAudioStreamComponent`

### 1.4 🟡 동시 발화 큐잉 (1~2 NPC) — M
- **갭**: 여러 NPC 가 동시에 말하면 TTS 합성/재생 충돌 가능성. 큐잉 정책 미검증.
- **할 일**: TTSService 측 동시 요청 직렬화 또는 NPC별 독립 스트림 보장. UE `NPCAudioStreamComponent` 의 다중 인스턴스 동시 재생 검증.
- **결정 필요(§0)**: 큐잉을 서버(TTSService)에서 할지, UE 재생 측에서 할지.
- **파일**: `TTSService/server.py::ws_stream`, UE `NPCAudioStreamComponent`

### 1.5 🟢 LLM 스트리밍 도입 재검토 — M
- **갭**: 현재 LLM 응답을 받은 뒤 TTS 합성(직렬). 첫 음 지연 큼.
- **할 일**: 지연 측정(아래 1.6) 결과 보고 → 문장 단위 스트리밍(LLM 토큰 → 문장 분할 → 점진 TTS) 도입 여부 판단.
- **트레이드오프**: 체감 지연↓ vs 구현 복잡도·문장 경계 처리.

### 1.6 🟡 지연 실측 기록 — S
- **갠**: 목표 < 600ms (Memo). 미측정.
- **할 일**: T0(PlayFromUrl)→T1(WS Connected)→T2(첫 청크)→Play() 실측. `NPCAudioStreamComponent` 의 `[T0/T1/T2]` 로그(이미 있음) 수치 수집.
- **파일**: UE `NPCAudioStreamComponent` (헤더에 `HandleConnected`/`PlayRequestedAt` 존재)

### 1.7 🟢 README/문서 최신화 — S
- **갭**: `TTSService/README.md` 가 "M1 Stub"(사인파) 기준 — 실제는 M2 OpenVoice. 오해 소지.
- **할 일**: README 를 v0.6.0 OpenVoice 기준으로 갱신.

### 1.8 🟢 M4 (선택) — 표정·립싱크·제스처 — L
- MetaHuman Lip Sync 또는 Audio2Face 에 오디오 스트림 분기
- AnimBP `LipSyncCurve`/`FacialExprParams`/`GesturePose` 입력
- `animation_metadata.emotion` → AnimBP `EmotionMood`
- (별도 R&D) StreamingTalker / Audio2Gesture / CAP4D

**TTS 권장 순서**: 1.1(감정복구) → 1.6(지연측정) → 1.3(trace) → 1.2(재시도) → 1.4(큐잉) → 1.5(스트리밍 판단) → 1.8(M4).

---

## 2. ASR — 잔여 (음성입력 M3)

핵심(M1/M2)은 완료·Develop 머지됨. 잔여는 UX·정리.

### 2.1 🟢 partial 실시간 자막 (M3) — M
- **갭**: 현재 `end` 시 final 단발(`ASRService/server.py:24`). 말하는 중 중간 인식 결과 없음.
- **할 일**: 청크 누적분을 주기적으로 부분 인식해 `partial` 메시지 전송 → UE 화면에 실시간 자막.
- **결정 필요(§0)**: 부분 인식 주기, whisper 재추론 비용 vs streaming-friendly 모델.

### 2.2 🟢 빠른 재누름 가드 — S
- **갭**: push-to-talk 빠른 재누름 시 이전 final 대기 소켓 orphan(테스트 로그 관찰). M1 무해하나 정리 가치.
- **할 일**: `VoiceInputComponent::StartTalking` 진입 시 기존 소켓 정리 후 시작.
- **파일**: `Core/VoiceInputComponent.cpp::StartTalking`

### 2.3 🟢 ChatWidget 레거시 정리 — S
- ASR 전환 완료 → 구 채팅 입력 코드 잔재 있으면 제거/정리.

---

## 3. 대화·인지 엔진 (Cognitive)

### 3.1 🟡 메모리·RAG 강화 — M
- `[RAG] No documents found for Skadi` 로그 관찰 — NPC별 지식 문서 미비. persona·lore 문서 채우면 대화 풍부.
- **파일**: `utils/rag_utils.py`, `utils/memory_manager.py`

### 3.2 🟢 모델 라우팅 점검 — S
- importance=normal→gemma4:e4b / high→12b / core→26b. 변경 시 `dialogue.py`/`main.py`/`debug.html` 3곳 동기(Memo Handoff).
- VRAM 동시 사용(gemma 26b + whisper large-v3) 모니터 — 부족 시 라우팅·모델 크기 조정.

### 3.3 🟢 ambient 평상 반응 튜닝 — S
- NPC ambient 모드(`main.py`/`dialogue.py`/`state.py`) 자연스러움 튜닝.

---

## 4. NPC AI / 액션 (에디터 작업 다수)

### 4.1 🔴 DA_NPC_Actions 몽타주 등록 — M (에디터)
- **갭**: LLM 이 `SitDown`/`Give`/`Eat`/`Emote_*` 등 액션 내보내도 몽타주 미등록이면 무음 실패.
- **할 일**: `Block`/`Dodge`/`SitDown`/`SitUp`/`LieDown`/`LieUp`, `Give`/`PickUp`/`Drop`/`Eat`/`Comfort`/`Craft`/`Repair`/`Pray`/`Read`, `Emote_*`/`Dance_*`/`Sing_*` 몽타주 할당.
- **참고**: PDF 방식(플레이어)과 무관 — NPC는 모션소스 없어 몽타주 필수.

### 4.2 🟡 BT→StateTree 마이그레이션 잔여 (에디터) — S
- `ST_NPC.uasset` Transition(`bHasAction==true`)·Evaluator 연결 확인.
- `BB_NPC.uasset` 죽은 키(HasAction/SubAction/BehaviorMode/FacialState) 제거.

### 4.3 🟡 EQS 에디터 바인딩 (빌드 후 필수) — S
- DistanceWeightParam/CoverWeightParam → Test Score Factor
- SafeDistance → Distance Filter Min
- AggressionWeightParam → TacticalPositionsQuery Inverse Distance Test
- (Memo Handoff "EQS Named Parameter" 참조 — Generator 반경은 고정값)

### 4.4 🟢 Blueprint CDO PerceptionTickInterval 리셋 — S (에디터)
- C++ 9.0f 추가됐으나 BP CDO가 3.0f 직렬화 중. NPC BP Details 에서 ↺ 리셋.

---

## 5. VR

### 5.1 🟢 발 IK (지면 밀착) — M
- 현재 FBIK Effector 는 Head·양손만. 발 뜸 발생 시 Foot IK / Body Mover 지면 정렬 추가.
- **주의**: 수동 Height Offset 과 자동 지면부착 충돌 시 점진 주저앉음(PDF §Stick to Floor) — 하나만 채택.

### 5.2 🟢 Neck exclude 조정 (선택) — S
- 현재 시야-머리 정렬을 `CameraHeightOffset=-30` 으로 해결. 키 손해 없이 하려면 Neck를 Excluded Bones 에서 빼 머리를 카메라까지 늘림.

### 5.3 🟢 멀티플레이어 자세 동기화 (Phase 6) — L
- `bReplicates`/ReplicateMovement/Component Replicates
- `EVRPosture` `ReplicatedUsing=OnRep_Posture` (Enum 만 전송)
- HMD/양손 Transform VROrigin 기준 Relative 직렬화 → Multicast RPC (World 금지)
- OnRep_Posture 에서 SetCapsuleHalfHeight + VInterpTo 원격 보간
- 참고: PDF §"멀티플레이어 리플리케이션·패킷 최적화"

### 5.4 ⏸️ Quest 스탠드얼론 (보류 — 타겟 확정 시)
- `Package → Android (ASTC)` → APK, `adb install` 사이드로딩
- LLM/TTS IP 외부화 → `DefaultGame.ini [OmniAgent]` (현재 127.0.0.1)
- AndroidManifest `CHANGE_NETWORK_STATE`
- **모바일 성능 최적화** 숙제 동반 (Quest 칩 제약)

---

## 6. UI

### 6.1 🟡 ChatWidget WorldSpace 부활 — M
- 현재 NPC 응답 = 화면 자막뿐. WorldSpace 위젯(또는 자막 위젯)으로 정식화.
- WristWidgetComp 처리, BP_ChatWidget "Is Variable" → World Space, VR용 폰트 확대.
- (스탠드얼론 시) Quest 시스템 키보드 — 음성입력(ASR)으로 대체됐으니 우선순위↓.

---

## 7. 플레이어 시스템

### 7.1 🟢 인벤토리·아이템 — M
- `Inventory/` (InventoryComponent, ItemManager, ItemDataAsset, DroppedItemBase) 완성도 점검.
- 무기 부착 = X_Bot `hand_r`/`hand_l` 본 소켓(VR FBIK 결정).

### 7.2 🟢 전투 — M
- VRPawn 공격(`AttackDamage`/`AttackRange`/`AttackMontage`), NPC Combat 자동 Attack(복구됨) 밸런싱.

---

## 8. 인프라 / 품질

### 8.1 🟢 지연·성능 대시보드 — S
- TTS/ASR/LLM 추론 시간 일관 로깅 → 디버그 대시보드(http://127.0.0.1:8000/debug) 확장.

### 8.2 🟢 테스트 — M
- Envelope 직렬화·액션 파이프라인 핵심 경로 단위 테스트(현재 수동 검증 위주).

### 8.3 🟢 문서 동기화 — S
- `docs/index.html` 의 TODO/code-review 앵커, TTS README, Memo 정합.

---

## 9. 권장 다음 스프린트 (제안)

1. **TTS 감정 복구 = TTSExtend 회수**(1.1 + §10) — 🔴 최우선. 기능이 이미 작성돼 있고(미머지) base_voices 데이터도 Develop 에 있음. 로직만 포팅하면 즉시 감정 음색. 체감 큼.
2. **TTS 지연 측정 + trace**(1.6, 1.3) — 이후 모든 음성 작업의 계측 기반. request_id 통로 이미 존재.
3. **TTS 재시도**(1.2) — 타임아웃 이미 있음, 재시도만 추가(소).
4. **DA_NPC_Actions 몽타주 등록**(4.1) — LLM 액션 표현력 병목(에디터).
5. **ChatWidget WorldSpace**(6.1) — 음성 응답 가시성.
6. 이후 NPC ambient 회수(§10) / ASR partial(2.1) / 멀티플레이(5.3) 중 선택.

### 9.1 착수 스펙 — TTS 감정 복구 (1순위, 바로 시작 가능)
**목표**: 화난 대사가 화난 음색으로 나오게.
**단계**:
1. `git show feature/TTSExtend:OmniAgent_VR_System/TTSService/voice_resolver.py` → 현재 OpenVoice server 와 호환되게 Develop `voice_resolver.py` 교체(또는 `resolve_voice_meta`/`normalize_emotion` 이식).
2. `TTSService/server.py`: `SynthesizeRequest.emotion` 을 `_synthesize_sync` 까지 전달 — `resolve_voice_meta(voice_id, emotion)` 로 ref/speed 선택, `_synthesize_sync(text, ref, language, speed)` 적용.
3. `main.py:358-384`: Dialogue 액션 탐색 시 `act.FacialState` 를 잡아 `_dispatch_npc_audio(emotion=act.FacialState)`.
4. `TTSService/debug.html` 복사(무충돌) — 감정별 합성 즉석 테스트용.
**검증**: debug.html 에서 Skadi/Angry 합성 → angry ref·speed. 게임 내 화난 대사 → 음색 변화. 로그 `emotion=Angry`.
**리스크**: TTSExtend voice_resolver 가 가정하는 server 함수 시그니처가 현재(v0.6.0)와 다를 수 있음 → 이식 시 `_synthesize_sync` 인자 정합 먼저 맞출 것.

---

## 10. 🔴 TTSExtend 미머지 작업 회수 (Stranded Work Recovery)

> **배경**: `feature/TTSExtend` 는 VR/리팩토링 머지 **이전** 옛 지점에서 분기됨. 통째 머지하면
> VRPawn(701줄)·OmniAgentConfig·PlayerGameplayTags 등이 **revert** 됨 → 절대 머지 금지.
> 대신 아래 기능들을 **Develop 현재 코드 위에 수동 재적용**(cherry-pick 은 main.py/dialogue.py 충돌 큼).

| 커밋 | 기능 | Develop 상태 | 회수 방법 | 우선 |
|------|------|--------------|-----------|------|
| `b11233d` | **감정 TTS** (voice_resolver_meta·base_voices·정규화) | ❌ 없음(감정 무시) | §1.1 — voice_resolver/server emotion 부분 포팅 | 🔴 |
| `b11233d` | TTS **debug.html** UI | ❌ 없음 | 파일 단독 복사(`TTSService/debug.html`) — 무충돌 | 🟢 |
| `9b461b5` | **NPC ambient 평상 반응** 모드 | ❌ 없음(main.py 0곳) | main.py/dialogue.py/state.py 에 수동 재적용(현재 코드와 diff 비교) | 🟡 |
| `b6833ed` | ASR 스텁 | ✅ 회수됨(cherry-pick) | 완료 | — |
| `f99c0d3` | ChatWidget 제거 | ✅ Develop 반영됨 | 완료 | — |

**회수 절차 권장**:
1. `git show <commit> -- <파일>` 로 변경 내용만 추출.
2. Develop 현재 파일에 **의미 단위로 수동 반영**(특히 main.py 는 그새 bc16134·50add82 등으로 많이 바뀜).
3. base_voices WAV(Skadi_angry 등)는 **이미 Develop 에 머지됨**(VR 머지 때 동반) — 데이터는 있고 로직만 없음.
4. 회수 후 TTSExtend 브랜치는 **삭제 또는 아카이브**(혼동 방지).

---

## 부록 — 결정 대기 항목 (§0 Ask-Before-Choose)
- TTS 동시발화 큐잉 위치: 서버 vs UE 재생측 (1.4)
- LLM 스트리밍 도입 여부 (1.5, 지연측정 후)
- ASR partial 주기·방식 (2.1)
- Quest 최종 타겟: PCVR 고정 vs 스탠드얼론 (5.4 — 전체 IP/성능 작업 분기)
