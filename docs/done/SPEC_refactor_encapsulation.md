# 전수조사 보고서 — 캡슐화·가독성·코드량

작성일 2026-09-11. 이전 판(grep 샘플링)을 폐기하고, 이번엔 소스를 전부 읽고 다시 썼습니다.

읽은 범위: `Source/` C++ 전체(16,874줄), `OmniAgent_VR_System/CognitiveEngine/app`(약 5,500줄), TTS·ASR 서비스(약 1,200줄), `tools/`(약 2,500줄, 파이프라인 스크립트), `tests/`(약 1,100줄). 파인튜닝 학습 스크립트(`finetune/`)는 제외했습니다 — 오프라인 파이프라인이고 이 보고서의 목적(런타임 코드 가독성)과 무관합니다.

"죽었다"고 적은 것은 전부 grep 으로 호출처 0건을 확인했고, BlueprintCallable/Pure 인 것은 `.uasset` 바이트코드까지 뒤졌습니다. "아마도"라고 쓴 건 확인 못 한 겁니다.

---

## 1. 먼저 전체 인상

코드 자체는 꽤 좋습니다. 주석에 "왜"가 적혀 있고, 위험한 지점마다 실측 날짜가 붙어 있고, 비동기 완료 모델이나 EQS gen 규칙처럼 한 번 잘못 건드리면 조용히 깨지는 부분은 규칙으로 못 박아 뒀습니다. 이런 코드베이스에서 "가독성을 늘리고 코드를 줄이는" 일은 대개 새 구조를 넣는 게 아니라 **쌓인 것을 걷어내는 일**입니다.

읽고 나서 남은 결론은 세 가지입니다.

첫째, **죽은 코드가 생각보다 많습니다.** 함수·클래스·필드·모듈을 합쳐 50건 넘게 나왔습니다. 대부분 BT→StateTree, SLM 반사→C++ 테이블, 2-폰→1-폰처럼 큰 전환이 지나가면서 남긴 잔해입니다. 이건 위험이 거의 없고 이득은 확실합니다.

둘째, **"설계는 했는데 배선을 안 한" 기능이 한 층 깔려 있습니다.** VR 제스처·플레이어 스탯·`last_event`·`action_failed`·`perceived_targets`… Python 은 받을 준비가 돼 있고 C++ 은 보낼 필드가 있는데, 실제로 보내는 코드가 없습니다. 이건 삭제할지 로드맵에 남길지 결정이 필요한 영역이라 목록만 정리했습니다.

셋째, **같은 일을 두세 번 하는 곳이 꽤 있고, 그중 일부는 캡슐화 문제입니다.** 예를 들어 `ADroppedItemBase` 의 물리를 켜고 끄는 코드가 네 군데 흩어져 있습니다. 아이템이 자기 물리 상태를 스스로 관리하면 네 곳이 한 줄씩이 됩니다. 이런 게 진짜 "객체지향적으로 캡슐화해서 줄이는" 사례입니다.

반대로, **크게 쪼개야 할 것은 없습니다.** `AVRPawn` 1,819줄, `UNPCActionComponent` 2,398줄이 눈에 띄지만, 앞 세션에서 결론 낸 대로 컴포넌트로 쪼개는 건 BP 재배선 비용 대비 얻는 게 없습니다. 이번에 전부 읽고 나서도 그 판단은 그대로입니다. 다만 그 두 파일 **안에서** 걷어낼 것은 꽤 있습니다.

---

## 2. 코드베이스를 관통하는 패턴

파일별로 들어가기 전에, 여러 파일에 걸쳐 반복되는 것들을 먼저 봅니다. 이걸 먼저 고치면 파일별 소견의 절반은 같이 해결됩니다.

### 2-1. 서브시스템을 꺼내는 코드가 세 벌입니다

`UNPCManager::Get`, `UItemManager::Get`, `UFurnitureManager::Get` 은 본문이 한 글자도 안 다릅니다(WorldContext → World → GameInstance → GetSubsystem). 그리고 그 `::Get` 이 있는데도 `GetGameInstance()->GetSubsystem<UItemManager>()` 를 인라인으로 쓰는 곳이 VRPawn 2곳, NPCActionComponent 3곳, InventoryComponent·DroppedItemBase·FurnitureActor·NPCManager 에 하나씩 더 있습니다. 같은 파일 안에서 두 방식이 섞여 있기도 합니다(VRPawn 1104행은 인라인, 1443행은 `::Get`).

템플릿 하나면 됩니다.

```cpp
// Core/Utils/SubsystemUtils.h
template<class T> T* GetGameSubsystem(const UObject* Ctx)
{
    if (!Ctx || !GEngine) return nullptr;
    UWorld* W = GEngine->GetWorldFromContextObject(Ctx, EGetWorldErrorMode::ReturnNull);
    UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<T>() : nullptr;
}
```

세 `::Get` 은 이걸 부르는 한 줄로, 인라인 조회 8곳은 `UItemManager::Get(this)` 로. 30줄 남짓 줄고, 더 중요한 건 앞으로 "서브시스템은 이렇게 꺼낸다"가 한 가지가 됩니다.

### 2-2. WebSocket 을 닫는 코드가 세 벌입니다

`UWebSocketClient::UnbindSocketEvents`, `UVoiceInputComponent::CloseSocket`, `UNPCAudioStreamComponent::Stop` — 전부 "네 델리게이트 Clear → 연결 중이면 Close → Reset" 입니다. `IsModuleLoaded("WebSockets") ? : LoadModule` 도 VoiceInput 과 AudioStream 에 있고 WebSocketClient 는 `LoadModuleChecked` 를 씁니다.

`WebSocketUtils::SafeClose(TSharedPtr<IWebSocket>&)` 하나와 `EnsureModule()` 하나. VoiceInput 의 abort 시퀀스(2-9 참조)까지 합치면 이 영역에서 30줄쯤 빠집니다.

### 2-3. httpx 클라이언트 싱글턴이 세 벌입니다 (Python)

`main.py::_get_ollama_client`, `llm_factory.py::_get_structured_client`, `tts_client.py::_get_client` — 셋 다 "None 이거나 닫혔으면 새로 만들고 반환". 앞의 둘은 같은 Ollama 서버를 향합니다. 최소한 그 둘은 `llm_factory` 하나로 합쳐야 하고, `tools/comfyui_mcp/server.py` 에도 같은 게 하나 더 있습니다.

### 2-4. "가장 가까운 것 찾기" 루프가 네 벌입니다

`VRPawn::TryPickupNearby`, `VRPawn::FindNearestItemNearHand`, `PlayerInteractionUtils::FindNearestNPCId`, `FurnitureManager::FindNearestVacantSitable` — 전부 "후보 순회 → DistSquared 비교 → 최소값 갱신". 이건 억지로 템플릿화하면 오히려 읽기 어려워지는 부류라 강하게 밀지는 않겠습니다. 다만 `TryPickupNearby` 와 `FindNearestItemNearHand` 는 같은 파일에서 같은 `ItemManager->GetItemsInRange` 를 돌리는 거라 하나로 합쳐야 합니다(2-6 참조).

### 2-5. 마찰을 0 으로 만들고 발사하는 코드가 두 클래스에 있습니다

`AVRPawn::OnDash/StopDash` 와 `UNPCActionComponent::StartDodgeMove/StopDodgeMove`. 둘 다 `GroundFriction/BrakingDecelerationWalking/BrakingFrictionFactor` 세 값을 저장 → 0 → `LaunchCharacter` → 복원 시 `Velocity.X/Y = 0`. 저장용 멤버 세 개까지 이름이 같습니다.

앞 세션에서 VRPawn 쪽만 보고 "구조체는 과설계"라고 했는데, 두 클래스에서 같은 일을 하는 걸 보면 얘기가 다릅니다. `MovementUtils::BeginFrictionlessLaunch(CMC, Dir*Speed, Saved&)` / `EndFrictionlessLaunch(CMC, Saved)` 두 함수와 `FSavedFriction` 구조체 하나. 두 클래스에서 합쳐 40줄쯤 빠지고, 멤버 6개가 2개로 줄어듭니다.

### 2-6. 아이템을 줍는 코드가 두 클래스에 있습니다

`AVRPawn::TryPickupNearby` 와 `UNPCActionComponent::PerformPickupAtDestination` — `GetItemsInRange` → `Cast<ADroppedItemBase>` → `GetItemDataByID` → `Inventory->AddItem` → `ConsumeItem`. 플레이어 쪽은 최근접 하나만, NPC 쪽은 첫 성공 하나만. 차이는 그것뿐입니다.

이건 아이템 쪽으로 옮기는 게 맞습니다. `ADroppedItemBase::TryPickupInto(UInventoryComponent* Inv)` — 데이터 조회·AddItem·Consume 을 아이템이 스스로 하고 bool 만 돌려주면, 두 호출처는 "후보 찾아서 `TryPickupInto`" 한 줄이 됩니다.

### 2-7. 아이템 물리를 밖에서 만지는 곳이 네 군데입니다 ← 캡슐화 핵심

이게 이번 조사에서 가장 "객체지향적인" 문제입니다.

- `UInventoryComponent::AttachItemToHand` — `SetSimulatePhysics(false)` + `NoCollision` + 상호작용 구체 끄기
- `UInventoryComponent::StoreHeldItem` 의 `DropWhereItIs` 람다 — 반대로 켜기
- `ADroppedItemBase::LaunchThrown` — 프로파일 재지정 + 채널 응답 + 켜기 + Hit 이벤트
- `ATradeSessionActor::SetItemLocked` — 잠글 때 끄기, 풀 때 프로파일·채널·켜기

네 곳이 각자 `ItemMesh->Set...` 을 조금씩 다르게 부릅니다. `TradeSessionActor` 의 주석은 심지어 "쥐기 해제와 같은 복구 절차 — 프로파일을 다시 지정해야 채널 응답까지 돌아온다"고 적어 두고 있습니다. 이 지식이 `ADroppedItemBase` 안에 있어야 합니다.

```cpp
// ADroppedItemBase
void SetPhysicsFrozen(bool bFrozen);   // 쥠·접시 잠금 = true, 놓음·던짐·취소 = false
```

이 한 함수가 물리·콜리전·프로파일·상호작용 구체를 일관되게 처리하면, 네 호출처는 각각 한 줄이 되고, "채널 응답이 안 돌아오는" 류의 버그가 한 곳에서만 나게 됩니다. 이 항목은 줄 수(약 30줄)보다 **버그 표면적 축소**가 이득입니다.

### 2-8. 가구에 앉히는 코드가 두 클래스에 있습니다

`AVRPawn::TrySitOnNearbyFurniture` 와 `UNPCActionComponent::PlayActionMediaWithPosture` — 둘 다 `TryOccupy` → `GetSeatTransform` → `SetActorLocationAndRotation(..., TeleportPhysics)`. 2-7 과 같은 논리로 `AFurnitureActor::TryOccupyAndSeat(AActor*)` 로 가구가 앉히면 됩니다. 플레이어는 Yaw 만 쓰고 NPC 는 전체 회전을 쓰는 차이가 있으니 `bYawOnly` 인자 하나.

### 2-9. VoiceInputComponent 의 abort 시퀀스

`OnConnectionError` 와 `OnClosed` 람다가 `bTalking=false` + StopStream/CloseStream + `SetComponentTickEnabled(false)` 를 인라인으로 씁니다. 바로 아래에 `StopAudioCaptureStream()` 헬퍼가 있는데 안 씁니다. `AbortStreaming()` 하나로. (앞 세션 R2 와 동일 — 이번에 다시 확인했고 그대로입니다.)

### 2-10. 가중치 추첨이 두 벌입니다

`UNPCActionComponent::PickWeightedReflexAction`(TMap 대상, static) 과 `SelectCombatAction` 안의 `PickWeighted` 람다(TArray 대상). 알고리즘이 같습니다 — 합 구하고, 굴리고, 빼 나가고, 부동소수 잔여 폴백. 람다를 static 함수로 빼서 둘이 같이 쓰면 15줄.

### 2-11. 엔진 기본 도형 + 이미시브 머티리얼 로딩이 세 군데입니다

`VRPawn::BeginPlay` 의 PointerBeam 블록, 같은 함수의 VoiceLevelOrb 블록, `TradeSessionActor` 의 `LoadCube/TintMesh`. 전부 `LoadObject<UStaticMesh>(Sphere|Cylinder|Cube)` + `EmissiveMeshMaterial` + `MID::Create` + `SetVectorParameterValue("Color")`. 작은 유틸 하나(`EngineShapes::MakeEmissiveMID(Outer, Color)`)로 각 10줄 → 2줄.

### 2-12. 다음 틱으로 미루는 코드가 네 군데입니다

`SetTimerForNextTick` + `TWeakObjectPtr` 캡처 패턴이 NPCManager 2곳, NPCAudioStreamComponent 2곳. 이유도 같습니다("broadcast 루프 안에서 소켓을 건드리면 ensure"). `DeferToNextTick(this, [WeakThis]{...})` 헬퍼로.

### 2-13. Python 은 print 와 logger 가 섞여 있습니다

`main.py`·`middleware.py`·`db_manager.py`·`async_tasks.py` 는 `logger`, `dialogue.py`·`rules.py`·`supervisor.py`·`interface_*.py`·`rag_utils.py`·`memory_manager.py`·`train_logger.py` 는 `print`. W28 에서 cp949 파이프에서 print 이모지가 노드를 통째로 죽인 사고가 있었습니다. 남은 print 를 전부 `logging.getLogger(__name__)` 으로 바꾸는 건 가독성보다 재발 방지입니다.

### 2-14. Envelope 을 만들 때 JSON 을 두 번 직렬화합니다

`FEnvelopeBuilder::BuildEnvelope` 이 `PayloadJson` **문자열**을 받아서 역직렬화한 뒤 다시 직렬화합니다. 호출처(NPCManager·NPCActionComponent·LLMNetworkClient)는 이미 `FJsonObject` 를 손에 들고 있고, 그걸 문자열로 만들어 넘기고, 빌더가 다시 객체로 풀어 봉투에 넣고, 다시 문자열로 만듭니다. 시그니처를 `TSharedRef<FJsonObject>` 로 바꾸면 세 호출처의 Writer 4줄과 빌더의 Deserialize 10줄이 없어지고, 메시지마다 파싱 한 번씩이 사라집니다.

---

## 3. 파일별 소견

### 3-1. NPC / Action

**`NPCActionComponent.{h,cpp}` (724 / 2,398줄)**

가장 큰 파일이지만 구조는 명확합니다. `ExecuteInteraction` 의 큰 switch 는 파라미터를 한 번 풀어서 `Execute*` 로 흘려보내는 디스패처이고, `Execute*` 는 `Base*` 를 조합하고, `Base*` 는 엔진을 건드립니다. 이 세 층은 유지하는 게 맞습니다.

걷어낼 것:

- `BaseEmote`·`BaseDance`·`BaseSing`·`BaseSignalAllies` — 넷 다 본문이 `BasePlayActionMedia(x)` 한 줄이고, BP 에서 호출하는 곳도 없습니다(`.uasset` 0건). 호출처에서 `BasePlayActionMedia` 를 직접 부르고 넷을 지우면 헤더 8줄 + cpp 4줄.
- `BaseSendEventToActor` — TODO 스텁, 로그 한 줄뿐, 호출 0. `BaseDetectEntityInRange` — 호출 0. 둘 다 삭제.
- `ExecuteAttackAction(TargetActor, EAttackType)` — `EAttackType` 인자를 받지만 읽지 않고, `Range/Magic` 은 어디서도 안 씁니다. 인자 제거, enum 은 남겨도 무방.
- `BaseMove` 와 `BaseMoveToActor` — 속도 세팅·PFC 바인딩·`bActionAwaitingAsync`·`HandleImmediateMoveResult` 까지 20줄이 같고, `MoveToLocation` 이냐 `MoveToActor` 냐만 다릅니다. 공통 전처리를 `PrepareMove(OwnerChar&, AIController&)` 로 빼면 두 함수가 각 8줄.
- `ExecuteTrack` 과 `UpdateTrackPosition` — 둘 다 "Pawn 캐스트 → AIController 캐스트 → MoveToActor(Target, 150)". 그리고 "TrackTimer 클리어 + TrackedTarget.Reset()" 이 네 군데(`StopAllActions`·`ProcessNextAction`·`AbortCurrentAction`·`UpdateTrackPosition`). `StopTracking()` 하나로.
- `ReportNoiseEvent` 호출이 `BaseDialogue`·`ExecuteUseItem`·`ExecuteAttackAction`·`ExecuteDrop` 에 있고 상수만 다릅니다. `ReportOwnerNoise(Loudness, Tag)` 하나로. 그런데 이걸 하다 보니 **`ExecuteDrop` 만 태그를 안 넘깁니다**(2214행). 청각 쪽은 태그를 `"Drop:0.4"` 로 쪼개서 EventType 을 뽑는데, 태그가 없으면 EventType 이 빈 문자열이라 반사 룰 "물건 떨어지는 소리 조사"(`EventTypeContains="Drop"`)가 NPC 가 떨어뜨린 소리엔 절대 안 걸립니다. 버그입니다. `NoiseTag_Drop` 상수까지 만들어 두고 안 쓴 겁니다.
- `Cast<ACharacter>(GetOwner())` 가 10번 넘게 나옵니다. `BeginPlay` 에서 `OwnerCharacter` 를 캐시하면 각 함수의 첫 두 줄이 사라집니다. `GetOwnerAIController` 는 이미 헬퍼인데 owner 는 매번 캐스팅하는 게 비대칭입니다.

EQS 파이프라인(1,160~1,590행), 전투 셀렉터(1,795~2,023행), 반사 테이블(55~239행)을 별도 클래스로 빼는 건 앞 세션 판정대로 **파일럿 1건(EQS)만**. 이번에 다 읽고 보니 세 덩어리가 전부 `ActionQueue` 에 직접 `Enqueue` 하고 `LastQueuedActionType` 을 건드립니다. 빼면 큐 주입 API 를 따로 열어야 하고, 그러면 결합이 줄어드는 게 아니라 인터페이스가 하나 늘어납니다. 굳이 뺀다면 EQS 만, 그것도 "ActionQueue 는 코어가 갖고 EQS 는 결과를 콜백으로 돌려준다" 형태로.

**`SmartNPCAIController.{h,cpp}` (105 / 528줄)**

- `HandleCombatTargetDead` 와 `HandleCombatTargetLostTimeout` — 주석 스스로 "동일 시퀀스에서 승리 보고만 제외"라고 씁니다. `ExitCombat(AActor* DeadTarget /*nullptr=소실*/)` 하나로 합치면 15줄.
- `FPerceptionData` 를 손으로 채우는 코드가 세 번(시야·청각·틱) + `SmartNPC::TakeDamage` + `NPCStateComponent::ReportCombatVictory` 까지 다섯 번. 5필드 생성자 하나면 각 6줄 → 1줄.
- `Cast<ASmartNPC>(GetPawn())` 6회. `OnPossess` 에서 캐시.
- `PerceptionTickTimer` 클리어 + `CurrentSightTarget.Reset()` 3회 → `StopSightTracking()`.
- `HandleActionStarted` — 본문이 비어 있습니다. 바인딩된 델리게이트 `OnActionStarted` 의 구독자는 이것 하나. 핸들러·바인딩·델리게이트 선언·`ProcessNextAction` 의 Broadcast 까지 다 지우면 10줄.
- `OnTargetPerceptionUpdated` 170줄은 `HandleSight/HandleHearing/HandleLost` 세 함수로 쪼개면 읽기 훨씬 낫습니다. 줄 수는 안 줄지만 이 파일에서 가장 값진 가독성 개선입니다.

**`SmartNPC.{h,cpp}` (367 / 838줄)**

- 464~838행 래그돌 375줄은 자기완결적입니다. `GetMesh`·캡슐·CMC·`PhysicalAnim`·AI 정지/재개·NPCManager 카운터·`LastHit*` 만 쓰고, SceneComponent 를 소유하지 않습니다(`PhysicalAnim` 은 ActorComponent). `UNPCRagdollComponent` 로 빼기에 이보다 좋은 후보가 없고, VRPawn 의 물리 전투와 "hit-react" 개념을 공유할 여지도 있어 재사용 명분이 유일하게 있습니다. 튜닝 UPROPERTY 15개는 BP_SmartNPC Details 에서 값을 옮겨야 합니다(DoList).
- `GetNPCManager()` private 래퍼가 있는데 같은 파일에서 `UNPCManager::Get(this)` 도 직접 씁니다. 래퍼 삭제.
- "넉다운 중이면 타이머 클리어 + `ExitKnockdown` + Phase=None" 이 `EndPlay`·`HandleDeath`·`FinishGetUp` 세 번. `LeaveKnockdownPhase()` 하나로.
- `SetBlackboardBool` — BT 시절 "IsConnected 키" 용. 호출 0, BP 0. 삭제.
- `HandlePlanUpdated` — 로그 한 줄 찍는 델리게이트 구독. `bPlanUpdatedBound` 플래그까지 달려 있습니다. 디버그 목적이면 `SetCurrentPlan` 에 로그 한 줄 넣고 구독을 지우는 게 낫습니다.

**`NPCStateComponent.{h,cpp}` (256 / 277줄)**

- `TryReflexAction(int Difficulty)` — W17 주사위 반사. W34 반사 테이블이 대체했고 호출 0. 삭제. 이걸 지우면 `UDiceSystem::RollD100` 의 유일한 호출처도 사라져 같이 삭제.
- private `GetOwnerAIController()` — 정의만 있고 호출 0. 삭제.
- `GetAttributes()` 가 `FNPCAttributes` 를 **값으로** 반환합니다. 구조체가 크고(스탯 5묶음), `SelectCombatAction`·`ComputeEQSWeights`·`ParseMoveSpeed` 가 매 호출마다 복사합니다. `GetMutableAttributes` 처럼 참조 반환으로.
- 이 컴포넌트는 이름이 "State" 인데 정작 속성(`NPCAttributes`)은 `ASmartNPC` 가 갖고 있고 컴포넌트는 `Cast<ASmartNPC>(GetOwner())` 로 빌려 씁니다(5회). 컴포넌트 안으로 옮기면 캐스팅이 사라지고 이름값을 하게 되지만, `EditAnywhere` 라 BP 인스턴스 값이 다 날아갑니다. 지금은 보류, 스탯 시트를 손볼 때(3-6 참조) 같이.

**`NPCManager.{h,cpp}` (162 / 642줄)**

- `UNPCMap` 은 `TMap<FString, ASmartNPC*>` 를 감싼 클래스인데, `UNPCManager` 가 그 메서드를 전부 다시 감쌉니다 — `RegisterNPC→NPCMap->RegisterNPC`, `GetNPCById→NPCMap->GetValidNPC`, `OnWebSocketMessageReceived→NPCMap->OnWebSocketMessageReceived`, `GetActiveNPCs→NPCMap->GetActiveNPCs`. 그리고 모든 호출에 `if (NPCMap)` 가드. `UNPCMap` 이 하는 일 중 매니저가 직접 못 할 게 없습니다. TMap 을 매니저 멤버로 올리고 `UNPCMap` 을 지우면 클래스 하나·래퍼 6개·널가드 8개가 사라집니다. 약 60줄. 이건 "감싼 게 아니라 한 번 더 건넌 것"입니다.
- `OnLLMMessageReceived` 225줄이 `{ if (Parse…) { …; return; } }` 블록 다섯 개입니다. `HandleAffinityUpdate/HandleDebugPrompt/HandleNpcAudio/HandleLocationDecision/HandlePlanResponse` 로 쪼개면 각각이 한 화면에 들어옵니다.
- `SendPlayerDialogue` 145줄도 plan JSON·인벤 JSON·valid_targets·nearby_furniture 네 블록입니다. 각각 `Build*Json` 헬퍼로. 가구 타입→문자열 switch 는 `UEnum::GetDisplayValueAsText` 로 대체 가능.
- `UMCPJsonUtils::ParseNpcAudioResponseFromObject` 가 `OutEmotion` 을 채워 주는데 매니저는 그 값을 안 씁니다.

**`STTask_*` (98+118 / 48+107줄)**

- `FSTEvaluator_NPCState` — `ST_NPC.uasset` 에 0건. 안 쓰는 Evaluator 입니다. 구조체·InstanceData·Link/Tick/TreeStart 전부 삭제(45줄).
- `FSTTask_PrepareNextActionInstanceData` 의 `SubAction`·`TargetLocation` 출력 — 주석은 "ExecuteSmartAction 이 참조"라는데, `ExecuteSmartAction` 은 `ActionComp->GetCurrentAction()` 을 직접 읽습니다. 에셋 바인딩 여부는 바이트코드로 확정 못 했습니다(`bHasAction` 도 1건으로 찍혀 구분이 안 됨). ST 에디터에서 두 핀이 어디에도 안 물려 있으면 삭제.

**`NPCAudioStreamComponent` · `NPCDialogueUIComponent` · `NPCInventoryComponent` · `NPCAnimInstance`**

- `NPCDialogueUIComponent` 는 이 코드베이스에서 가장 잘 캡슐화된 클래스입니다. 표시·타이머·싱크·빌보드·틱 수명이 전부 안에 있고 액터는 `ShowSubtitle` 만 부릅니다. 다른 컴포넌트를 만들 때 이걸 기준으로 삼으면 됩니다. 딱 하나, `bSubtitleWaitingForAudio` 는 네 곳에서 쓰기만 하고 읽는 곳이 없습니다. 삭제.
- `NPCInventoryComponent` — 생성자와 `BeginPlay` 가 `Super` 호출 + 주석뿐입니다(8줄). `GetInventoryJson` 은 슬롯용·장비용 JSON 조립이 8줄씩 복붙이라 람다 하나로.
- `NPCAudioStreamComponent` — 2-2, 2-12 해당. 그 외 깔끔합니다.

**`NPCActionTypes.h` · `NPCActionKeys.h`**

- `ENPCBehaviorMode` 의 `Social/Task/Investigation/Lifestyle` — C++ 어디서도 안 씁니다(`Combat`·`Common` 만). Python `NPCBehaviorMode` Literal 과 계약 테스트가 물려 있어 지우려면 양쪽 동시. 지금은 "C++ 로직상 죽어 있음"으로만 기록.
- `Media_Track`·`Media_Death`·`Audio_Gesture`·`Audio_LookAtPlayer`·`Audio_StreamMode`·`Audio_RequestId` — 여섯 상수 사용처 0. 삭제.
- `FLocationCandidate::CandidateId` 주석의 `"SAFE_0"` — 실제 값은 `"SAFE"`. 주석 정정. (Python `_location_decision_fast_path` 의 `"OPTIMAL_0"` 폴백도 같은 잔재 — C++ 이 모르는 ID 라 첫 항목 폴백으로 우연히 동작합니다. `"OPTIMAL"` 로 고칠 것.)

### 3-2. Core / VR

**`VRPawn.{h,cpp}` (856 / 1,819줄)**

전면 분해는 안 합니다(앞 세션 결론 유지). 안에서 걷어낼 것:

- `SendNPCDialogue` 와 `SayToNpc` — Exec 두 개가 거의 같은 일을 합니다(타겟 없으면 탐지, 있으면 전송). `SayToNpc` 가 transcript 경로를 재사용하므로 그쪽만 남기고 `SendNPCDialogue` 삭제.
- `OnMove` 와 `OnDash` 가 `CameraYaw → Forward/Right` 4줄을 각자 계산합니다. `GetHMDYawAxes(Forward&, Right&)` 하나로.
- 2-5(대쉬 마찰), 2-6(줍기), 2-8(착석), 2-11(엔진 에셋), 2-4(최근접) 해당.
- 손 L/R 짝 — `PrevHandLocLeft/Right`·`HandVelLeft/Right` 는 `FVRHandKinematics Hands[2]` 로 묶어도 됩니다(앞 세션 R3). 컴포넌트 포인터와 `GripOffset` 은 BP 가 이름·값을 갖고 있으니 그대로.
- 체크포인트 4멤버(`bHasCheckpoint`·`CheckpointLocation`·`CheckpointRotation`·`CheckpointHP`) + `PawnDeathUtils::SaveCheckpoint` 의 8인자 시그니처 — `FCheckpoint { bool bValid; FVector Loc; FRotator Rot; float HP; }` 하나로 묶으면 멤버 4→1, 인자 8→3. `PawnDeathUtils` 헤더 주석은 "VRPawn·VRPlayerCharacter 공통"인데 `VRPlayerCharacter` 는 이제 없습니다. 두 폰이 공유하려고 만든 out-param 잔치라, 폰이 하나가 된 지금은 구조체가 정답입니다.

**`PlayerHUDWidget.{h,cpp}` (218 / 381줄)**

- 헬스/스태미나 게터 6개는 앞 세션 R1 그대로 — WBP 바이트코드 0, C++ 0, 서로만 부름. 삭제.
- `UnequipSlot`·`GetEquippedSlotItem` 은 호출 0 이지만 장비 UI 미완성 훅이라 남깁니다.

**`VoiceInputComponent`** — 2-2, 2-9. 그 외 좋습니다.

**`Entity.h`** — 인터페이스 13개 메서드 중 실제로 `Execute_` 로 불리는 건 다섯(`GetEntityID`·`ApplyResourceDelta`·`GetPlayerAttributes`·`GetAgentID`·`GetItemID/IsPickupable` 는 구현만). `SetStateTag` 는 **구현하는 클래스조차 없습니다**(`_Implementation` 0). `GetEntityType`·`GetEntityLocation`·`GetNPCAttributes`·`GetPlayerName`·`GetAttributes`·`IsHostileTo` 는 구현은 있는데 인터페이스로 부르는 곳이 없습니다. W03 청사진의 "미래 API" 입니다. `SetStateTag` 는 지금 지워도 되고, 나머지는 BP 에서 오버라이드하는지 확인 후.

**`GameStateData.h`** — `FEntityState` 사용 0. `FGameStateData` 의 `PerceivedTargets/ThreatLevel/bIsInCover/bHasLineOfSight` 는 어디서도 채워지지 않고 기본값이 그대로 직렬화됩니다(3-6 참조).

**`CharacterAttributes.h`** — 3-6 에서 따로.

**`PawnDeathUtils` · `PlayerInteractionUtils` · `GameplayTagUtils` · `KineticDamage` · `KineticProjectile` · `Checkpoint`** — `KineticDamage` 는 근접·투사체·NPC 공격 셋이 공유하는 좋은 예입니다. `PawnDeathUtils` 는 위 체크포인트 구조체 건 외엔 그대로.

**`Utils/MCPMathUtils`** — `ConvertToUnrealLocation` 호출 0. 클래스 통째(2파일 25줄) 삭제. `DiceSystem::RollD100` 은 3-1 `TryReflexAction` 삭제와 함께.

### 3-3. Inventory

**`InventoryComponent.{h,cpp}` (340 / 954줄)**

응집도가 높은 파일이라 손댈 게 적습니다.

- `UnequipItemByID` 의 루프는 바로 아래 `GetSlotOfEquippedItem` 과 같습니다. 세 줄로.
- `BeginPlay` 만 `GetWorld()->GetGameInstance()->GetSubsystem` 인라인이고 나머지는 `UItemManager::Get(this)`. 통일.
- 구조 관찰 하나: "손에 든 것"이 두 계통입니다. NPC 는 `EquipItem` → `EquipmentSlots` + `AttachedMeshes`(장식 메시), 플레이어는 `TakeItemToHand` → `HeldItems`(물리 액터). `ActivateItem` 주석은 "무기든 잡템이든 전부 손에 쥔 물리 액터로 통일"이라고 씁니다. 즉 플레이어 쪽은 이미 `HeldItems` 로 통일됐고 `EquipmentSlots` 경로는 NPC 의 `ExecuteEquipAction/ExecuteHandObject` 만 씁니다. 언젠가 NPC 도 물리 액터를 쥐게 하면 `EquipmentSlots`·`AttachedMeshes`·`AttachEquipmentMesh/DetachEquipmentMesh` 약 120줄이 통째로 빠집니다. 지금은 기록만.

**`ItemManager.{h,cpp}` (85 / 187줄)**

- `GlobalItemDataTable` — `UPROPERTY(EditAnywhere)` 인데 GameInstanceSubsystem 은 에디터 Details 가 없습니다(W35 실측). 그래서 항상 null 이고, `GetItemDataByID` 는 **매 호출마다** `StaticLoadObject` 폴백 경로를 탑니다. 프로퍼티를 지우고 `Initialize()` 에서 테이블을 한 번 로드해 멤버에 캐시하면, 함수의 중복된 두 `FindRow` 분기가 하나가 되고 호출마다 에셋 로드도 사라집니다. 이건 가독성이자 성능입니다.
- `SerializeActiveItemsToJson` — TODO 스텁, 하드코딩 문자열 반환, 호출 0. `GetItemActorByID` — 호출 0. 삭제.
- `GetItemsInRange` 가 오버랩 결과마다 `ActiveDroppedItems` 를 선형 탐색합니다. 주석에 "액터에서 ID 를 꺼내면 O(1)"이라고 써 뒀는데, `ADroppedItemBase` 는 `ItemData` 를 갖고 있으니 `Cast` 한 번이면 됩니다. 그리고 호출처 전부가 받자마자 `Cast<ADroppedItemBase>(Candidate.ItemActor)` 를 합니다. 반환형을 `TArray<ADroppedItemBase*>` 로 바꾸면 선형 탐색과 호출처의 캐스트가 같이 사라집니다.

**`ItemDataAsset.h`** — `RepairItem()` 호출 0(W28 Repair 제거). 삭제. `BaseValue`·`Icon`·`CurrentDurability` 는 코드 참조 0 이지만 CSV 열이라 데이터 계약. 유지.

**`DroppedItemBase`** — 2-7 의 주인공. 그 외 2-1.

### 3-4. Network

**`WebSocketClient.h`** — 이 파일이 이번 조사에서 가장 명확한 "한 겹 더" 사례입니다. `UWebSocketClient`(소켓+재연결) → `UNetworkClientBase`(Abstract) → `ULLMNetworkClient`. 가운데 `UNetworkClientBase` 는 `UWebSocketClient` 를 멤버로 들고 그 델리게이트 두 개를 **같은 이름으로 다시 선언해 재브로드캐스트**하고, `bIsServerConnected` 로 `Socket->IsConnected()` 를 미러합니다. 추가하는 기능은 없습니다. Abstract 로 만든 건 `/ws/slm` 클라이언트가 따로 있던 시절이고, W22 단일 채널 이후로 자식은 `ULLMNetworkClient` 하나입니다. `ULLMNetworkClient : UWebSocketClient` 로 직접 상속하고 가운데를 지우면 40줄과 델리게이트 이중 홉이 사라집니다.

**`MCPJsonUtils`** — `ParseModeActionRequest(FString)`·`ParseLocationDecisionResult(FString)`·`ParseAffinityUpdate(FString)` 문자열 오버로드 세 개 호출 0(W17 parse-once 이후 `FromObject` 만 씀). 삭제 30줄. `TryParseBehaviorMode` 는 템플릿 한 줄 래퍼 — 삭제. 그리고 "문자열 → FJsonObject" 3줄이 이 파일 3곳 + NPCMap + NPCManager + VoiceInput + AudioStream 에 있습니다. `JsonUtils::Parse(const FString&)` 하나로.

**`EnvelopeBuilder`** — 2-14. 그리고 `BuildActionFailed` 호출 0(C++ 은 action_failed 를 보내지 않음). 함수 삭제, enum 값은 Python 계약 때문에 유지.

**`OmniAgentConfig.cpp`** — 네 개의 `bXLoaded + CachedX` 짝과 네 개의 "로드됐으면 반환, 아니면 읽고 마킹" 함수. `struct FCached { bool bLoaded; FString Token, Host; int32 LLMPort, TTSPort; }` + `EnsureLoaded()` 하나면 8개 전역이 1개, 함수 4개가 각 2줄. 약 25줄.

### 3-5. Furniture / UI

- `UFurnitureManager::FindNearestVacantSitable` 의 `FurnitureType != Seat && != Bed` 검사 — `EFurnitureType` 에 Seat 과 Bed 밖에 없어서 항상 거짓입니다. 두 줄 삭제.
- `AFurnitureActor::bOccupied` — `OccupantWeak.IsValid()` 의 미러. 주석도 "디버그 노출용 파생 미러"라고 인정합니다. `IsOccupied()` 가 이미 BlueprintPure 라 Details 노출 목적 외엔 없습니다. 지워도 되고, 디버그용이면 두되 `TryOccupy/Release` 두 곳에서 갱신하는 건 그대로.
- `TradeSessionActor::Accept` 의 두 전달 루프(플레이어→NPC, NPC→플레이어) — 방향만 다릅니다. `TransferAll(TArray&, Dest)` 람다로 15줄.
- `ItemTooltipWidget::RebuildWidget` 과 `NPCDialogueWidget::RebuildWidget` — "WidgetTree 없으면 만들고 → Border → VerticalBox → TextBlock 둘 + 폰트" 35줄이 거의 같습니다. 공통 베이스나 static 빌더 하나로 30줄.

### 3-6. 배선이 안 된 것들 (결정 필요)

이 묶음은 "지워라"가 아니라 "이게 아직 살아 있는 계획인지 정하라"입니다. 전부 C++ 이 보내는 코드가 없는 걸 grep 으로 확인했습니다.

| 항목 | Python 수신 측 | C++ 송신 측 | 상태 |
|---|---|---|---|
| VR 제스처 | `GestureData`, `_format_gestures`, `PromptPayload.gestures` | 없음 | W03 "VR 입력→Semantic Tag" 청사진. 배선 0 |
| 플레이어 위치·스탯·`last_event` | `_format_location`, `_emergency_interrupt`(Hit/Ambush), `PromptPayload.stats` | 없음 | `SendPlayerDialogue` 는 이 필드를 안 넣음 |
| `action_failed` | `_handle_action_failed`, `ActionFailedPayload`, `build_failed_event`, `failed_action_history`, `_format_failed_history`, 락 2개 | `BuildActionFailed` 정의만, 호출 0 | 양쪽 다 있고 연결만 없음. 약 80줄 |
| `perceived_targets` | `_format_perceived_targets`, `PerceptionData.in_line_of_sight/activity_context` | `FGameStateData.PerceivedTargets` 는 항상 빈 배열 | 항상 "None visible" |
| `state_update` 의 `threat_level/in_cover/line_of_sight` | `StateUpdatePayload` | 구조체 필드는 있으나 아무도 안 채움 | 기본값만 왕복 |
| `ENPCBehaviorMode` Social/Task/Investigation/Lifestyle | Literal 에 있음 | C++ 로직 사용 0 | 계약 테스트가 잠금 |
| `FCharacterAttributesBase` 스탯 시트 | — | `FCombatStats` 11필드 중 3개, Mana·Level·XP·Int/Wis/Luck/Sanity·JumpHeight·SwimSpeed·`EStatusEffect` 7종 사용 0 | RPG 청사진. BP 직렬화값 있음 |
| `AgentState.behavior_mode/facial_state/messages` | 초기화만 | — | 읽는 곳 0 |
| `AudioStreamInfo.mode`, `AnimationMetadata.gesture/look_at_player` | 직렬화됨 | C++ 파서가 안 읽음 | 와이어 낭비 |

제 의견은 이렇습니다. `action_failed` 는 양쪽이 다 있으니 **연결하거나 양쪽을 지우거나** 둘 중 하나여야지, 지금처럼 반쪽으로 두는 게 제일 나쁩니다. 제스처·스탯·`last_event` 는 VR 입력 로드맵이 살아 있다면 두고, 아니면 Python 쪽 포맷터 4개(약 60줄)와 스키마 필드를 지우면 됩니다. 스탯 시트는 BP 에 값이 박혀 있어서 정리 비용이 크니 스탯 재설계 때까지 그대로.

### 3-7. Python — CognitiveEngine

**`main.py` (1,107줄)** — 핸들러는 W27 에 잘 쪼개졌습니다. 남은 건:

- 822~1,092행 디버그·REST(약 270줄)를 `debug_routes.py` 의 `APIRouter` 로. 그러려면 `_active_llm_ws`·`_ws_send_lock`·`_handle_prompt` 를 공유해야 하는데, 모듈 전역 7개(`_cached_world_states`·`_failed_action_history`·락 2·`_active_llm_ws`·`_ws_send_lock`·`_ollama_client`·`_last_core_prewarm`)를 `@dataclass class ServerState` 하나로 모으면 라우터 분리가 깔끔해집니다. 이게 이 파일의 진짜 캡슐화 항목입니다.
- `_record_reflex_memory` 와 `_handle_combat_victory` 의 "메모리에 Event 한 줄 쓰는 스레드 오프로드 + 예외 로그" 블록이 같습니다. `_record_event_memory_bg(agent_id, text, label)` 하나로 15줄.
- `api_set_importance` 가 importance→모델명을 하드코딩합니다(`gemma4-12b`/`qwen3:8b`/`gemma4:e4b`). README 의 "모델 바꾸면 세 곳 고쳐라"는 규칙이 이 중복 때문에 생겼습니다. `llm_factory.model_for_importance()` 하나로 옮기면 그 규칙 자체가 없어집니다.
- `OLLAMA_BASE_URL.rstrip("/")` 가 세 함수에 있습니다. 정의 시점에 한 번.
- `import time as _t` 가 함수 안에 네 번, `import traceback` 이 상단에도 있고 except 안에도 있습니다. 상단으로.
- `"OPTIMAL_0"` 폴백 ID(3-1 참조).

**`dialogue.py` (473줄)** — 잘 정리돼 있습니다. `_run_stage1_llm` 의 두 `ollama_structured` 호출은 `for schema, temp in [(override, 0.5), (None, 0.7)]` 루프로 12줄 줄일 수 있지만, 지금 형태가 더 읽기 쉬울 수도 있어서 취향 문제입니다. 그보다 중요한 건 `_vr_get/_vr_player_id` 입니다(아래).

**`interface_input.py` (362줄)** — `_coerce_vr_context` 가 dict 를 `GesPrompt` 로 바꾸는데, **바꾼 결과를 state 에 돌려주지 않습니다.** 그래서 뒤 노드들(dialogue·rules)은 여전히 dict 일지 객체일지 모르고, 그 때문에 `_vr_get`·`_vr_player_id`(dialogue) 와 rules 의 인라인 `isinstance` 삼항이 존재합니다. `interface_input_node` 의 반환 dict 에 `"vr_context": vr_context` 한 줄을 넣으면 뒤에서는 항상 객체라, 세 모듈의 이중 대응 코드를 전부 `vr_context.player_id` 같은 직접 접근으로 바꿀 수 있습니다. 근본 원인 한 줄로 파생 코드 20줄이 빠집니다.
- `_extract_target_npcs` 의 폴백 리스트에 `"merchant"`·`"blacksmith"` — W28 에 유령으로 지운 이름들이 여기 남아 있습니다.
- `_format_gestures`·`_format_location`·`_format_perceived_targets`·`_emergency_interrupt` — 3-6.

**`rules.py` (387줄)** — `from .dialogue import _vr_get` 는 방향이 거꾸로입니다(검증 모듈이 생성 모듈에 의존). 위 수정으로 없어집니다. `"Heal"` 이 `_NUMERIC_CLAMP_RULES` 와 호감도 평가에 있는데 `EAction` 에 Heal 은 없습니다. 죽은 규칙. `_validate_batch` 안의 `from ...schemas.actions import GameAction` 은 상단 import 와 중복.

**`supervisor.py` · `interface_output.py`** — `_create_fallback_batch`(supervisor, Surprised/Confused) 와 `_create_empty_batch`(interface_output, Neutral) 가 거의 같은 폴백 배치입니다. `schemas/actions.py` 에 `fallback_batch(npc_id, facial="Neutral")` 하나. `"Elara"` 기본값이 네 군데 — `DEFAULT_NPC` 상수.

**`schemas/`** — `RejectResult`·`EQSQueryResult`·`Entity`(game_state) 사용 0. `PerceptionData.in_line_of_sight/activity_context` 사용 0. `config/world_constants.py` 모듈 통째로 import 0 — `actions.py` 의 `WORLD_CONSTANTS` dict 와 값도 다릅니다(`WORLD_BOUNDARY=50000` vs `x_max=10000`). `SharedData/world_constants.json` 도 rules.py 주석에만 나오고 로드하는 코드가 없습니다. 셋 다 삭제.
- `GesPrompt` 와 `PromptPayload` 는 필드가 거의 같고 `_build_prompt_state` 가 열 개를 손으로 복사합니다. 3-6 결정 후 `GesPrompt(PromptPayload)` 에 `timestamp` 만 더한 서브클래스로 하면 복사 10줄이 `GesPrompt(**payload.model_dump(), timestamp=...)` 한 줄.

**`utils/`** — `llm_factory.get_llm` 의 `OLLAMA_MODELS` set 은 `MODELS.keys()` 를 손으로 다시 적은 것입니다(14줄). `get_llm` 자체는 `memory_manager` 요약 한 곳만 씁니다. `keep_alive` 계산이 `get_llm` 과 `ollama_structured` 에 각각. `tts_client.aclose()` 호출 0. `db_manager` 의 reputation 태그 임계 계산 두 번, UPSERT SQL 두 번 — 각각 헬퍼·상수 하나. `middleware.py` 는 `datetime` 을 함수 안에서 세 번, `hmac` 을 한 번 import 합니다 — 상단으로.

### 3-8. TTS / ASR / tools / tests

- **TTS** `ws_stream` 과 `api_preview` 가 `resolve_voice_meta → ref or DEFAULT → lang or detect` 를 각자 합니다. `_resolve_synth_params()` 하나. `voice_resolver.list_voices` 는 `VoiceMeta(ref, lang.upper() if lang else None, float(speed) if speed…)` 를 세 번 씁니다 — `VoiceMeta.build()` 클래스메서드. `resolve_voice()` 호출 0. `KNOWN_EMOTION_LIST`(server) 와 `KNOWN_EMOTIONS`(resolver) 는 같은 9개 감정의 리스트/딕셔너리 판 — resolver 것에서 파생.
- **ASR** 깔끔합니다. `_send_error` 가 TTS 와 같은 모양인데 별도 프로세스라 그대로 둬도 됩니다.
- **tools/** — `upload_image` 가 두 스크립트에 동일, 큐 등록→history 폴링 루프가 세 곳(server·textures·meshes), `reimport_and_validate_datatable` 이 두 임포트 스크립트에 거의 동일. `tools/comfyui_mcp/_common.py` 하나로 100줄쯤. 일회성 스크립트라 우선순위는 낮습니다.
- **tests/** — `conftest.py` 가 `sys.path` 를 잡아 주는데(W28) 네 파일이 여전히 각자 `sys.path.insert` 를 합니다. `test_guardrail`·`test_rules_validation` 의 `run_all_tests()` 수동 러너는 pytest 이전 잔재. 봉투 팩토리가 세 파일에 각각(`make_*_envelope`·`_envelope`·`_build_prompt_envelope`) — `tests/_factories.py`.

---

## 4. 하지 말아야 할 것

읽고 나서 "이건 건드리면 손해"라고 판단한 것들입니다.

- **`AVRPawn` 컴포넌트 분해.** SceneComponent 여섯 종이 BP 에 이름으로 물려 있고, 폰은 하나뿐이고, 유닛테스트도 없습니다. 재사용·테스트 이득 없이 BP 재배선 비용만 듭니다.
- **`UNPCActionComponent` 전면 분해.** EQS·셀렉터·반사가 전부 `ActionQueue` 에 직접 씁니다. 뺀다면 EQS 하나만 파일럿으로, 콜백 경계로.
- **`UInventoryComponent`.** 954줄이지만 한 관심사이고 폰·NPC·거래가 공유합니다. 손댈 이유가 없습니다.
- **`FCharacterAttributesBase` 스탯 시트 축소.** 60% 가 미사용이지만 BP 인스턴스에 값이 박혀 있고 Python 스키마와도 얽혀 있습니다. 스탯을 재설계하는 날 같이.
- **`ENPCBehaviorMode` 값 삭제, `EEnvelopeType.ACTION_FAILED` 삭제.** 계약 테스트가 양쪽을 대조합니다. 지우려면 Python·C++·테스트를 한 커밋에.
- **`UnequipSlot`·`GetEquippedSlotItem`.** 호출 0 이지만 장비 UI 훅. 미완성이지 죽은 게 아닙니다.
- **디버그 Exec 함수들**(`TuneGrab`·`LogIKMetrics`·`DumpInventoryHUD`·`Cheat_Unequip`·`ToggleInventory`). 의도된 도구.

---

## 5. 어떤 순서로 할지

위험이 없는 것부터. 각 묶음은 Build.bat 또는 pytest 로 바로 검증됩니다.

**1단계 — 지우기 (C++ 빌드만, BP 무영향, 약 350줄)**

죽은 코드 목록(부록 A) 전부. 함수·클래스·필드·모듈 50여 건. 하나하나는 작지만 이걸 먼저 걷어내야 뒤의 통합이 깨끗해집니다. 커밋은 모듈 단위로 5~6개.

**2단계 — 세 벌짜리를 한 벌로 (C++ + Python, 약 250줄)**

2-1 서브시스템 Get, 2-2 WebSocket 닫기, 2-3 httpx, 2-5 마찰 발사, 2-9 Voice abort, 2-10 가중치 추첨, 2-11 엔진 에셋, 2-12 다음 틱, `OmniAgentConfig` 캐시, `MCPJsonUtils` JSON 파싱, db_manager 태그/SQL, voice_resolver `VoiceMeta.build`.

**3단계 — 캡슐화 (C++, 약 150줄이지만 버그 표면 축소가 본론)**

2-7 `ADroppedItemBase::SetPhysicsFrozen`, 2-6 `TryPickupInto`, 2-8 `TryOccupyAndSeat`, `FCheckpoint` 구조체, `UNPCMap` 흡수, `UNetworkClientBase` 제거, `ItemManager` 테이블 캐시 + `GetItemsInRange` 반환형, `FPerceptionData` 생성자, `ExitCombat`, `StopTracking`, `PrepareMove`.

**4단계 — Python 구조 (약 100줄 + 가독성)**

`interface_input` 의 vr_context 정규화 한 줄 → `_vr_get` 계열 제거, `ServerState` dataclass + `debug_routes.py` 분리, print→logger 일괄, `model_for_importance`, 폴백 배치·`DEFAULT_NPC` 통합, `GesPrompt(PromptPayload)`.

**5단계 — 결정 후 (3-6)**

`action_failed` 연결 or 양쪽 삭제, 제스처·스탯 로드맵 판단, `EnvelopeBuilder` 시그니처 변경(2-14 — 세 호출처 동시 수정이라 여기).

**6단계 — 큰 것 하나만**

`UNPCRagdollComponent` 추출(3-1 SmartNPC). BP Details 값 이전이 DoList 로 따라옵니다. 이건 해도 되고 안 해도 되는 항목입니다.

1~4단계까지 합치면 대략 850줄이 빠지고, 세 벌·네 벌짜리 코드가 한 벌이 됩니다. 새 클래스는 만들지 않고, 만든다면 `ServerState`·`FCheckpoint`·`FSavedFriction` 같은 값 묶음 셋 정도입니다.

---

## 6. 1단계 실행 계획 — 2026-09-12 확정

브랜치 `refactor/dead-code`(Develop 에서 분기). 이번 범위는 **1단계(삭제) + 부록 B 버그 2건 + 아래 결정 2건**. 2단계 이후는 별도 세션.

### 6.1 결정 (2026-09-12)

| 항목 | 결정 | 영향 |
|---|---|---|
| ASR/TTS 음성 파이프라인 | **삭제**(토글 OFF 아님). 검증 경로만 늘리고 VR 에서 채팅이 낫다는 판단(`bd057b8` 채팅 도입). 복원은 git | R2·2-2(Voice/Audio 분)·2-3(tts_client)·2-9·2-12(AudioStream 분)·3-8 TTS/ASR 항목 **전부 소멸** — 통합하지 않고 지운다 |
| `action_failed` | **양쪽 삭제** | C++ enum·`BuildActionFailed` + Python 핸들러·이력·락·포맷터·테스트 [5] + 계약 테스트 한 커밋 |
| VR 제스처·플레이어 스탯·`last_event` 수신측 | **유지**(로드맵 생존) | 3-6 표의 해당 행 손대지 않음 |
| `GlobalItemDataTable` 캐시(부록 B3) | 3단계로 | 삭제가 아니라 구조 변경 |

### 6.2 검증 전제

- **에디터 닫고 `Build.bat`**. 클래스·UPROPERTY 삭제는 Live Coding 으로 못 한다(레이아웃 변경 → 패치 불가/불안정). 각 커밋마다 exit 0 확인.
- Python 커밋은 `pytest tests -q` 통과.
- 자막 안전성: `UNPCManager::HandleNPCDialogue` 가 Dialogue 액션 경로에서 `ShowSubtitle(Text, false)` 로 이미 즉시 표시한다(2026-09-12 확인). `npc_audio_response` 를 통째로 지워도 자막은 남는다.
- 스팟체크(2026-09-12 grep): 부록 A 중 12건 재확인 — 전부 호출 0. `TryParseBehaviorMode` 는 호출 2곳 → 삭제 아닌 인라인.

### 6.3 커밋 순서 (위험 낮은 순, 에셋 무관 → 에셋 동반)

| # | 커밋 | 내용 | 검증 |
|---|---|---|---|
| C1 | `refactor: 미사용 HUD 게터 6개 삭제 — PlayerHUDWidget` | R1 그대로 | Build |
| C2 | `refactor: NPC 죽은 코드 삭제 — NPCStateComponent·SmartNPC·NPCActionComponent·SmartNPCAIController` | `TryReflexAction`+`RollD100`·`GetOwnerAIController`·`SetBlackboardBool`·`GetNPCManager` 래퍼·`BaseSendEventToActor`·`BaseDetectEntityInRange`·`BaseEmote/Dance/Sing/SignalAllies` 인라인·`HandleActionStarted`+`OnActionStarted`·`EAttackType` 인자·`HandlePlanUpdated`→`SetCurrentPlan` 로그 한 줄 | Build |
| C3 | `refactor: 미사용 미디어 키 상수 삭제 — NPCActionKeys` | `Media_Track`·`Media_Death` 만. **`FSTEvaluator_NPCState` 는 유지** — 2026-09-12 실측: `ST_NPC.uasset` 에 `NPC State Evaluator` 노드가 배치돼 있고 `bHasAction` 이 `StateTreeCompareBoolCondition` 에 바인딩됨(C++ grep 0 은 착시). `SubAction`/`TargetLocation` 출력 핀도 같은 이유로 보류 | Build |
| C4 | `refactor: Network 죽은 코드 삭제 — MCPJsonUtils·MCPMathUtils` | 문자열 오버로드 3개·`TryParseBehaviorMode` 인라인·`UMCPMathUtils` 2파일 | Build |
| C5 | `refactor: Inventory·Furniture 죽은 코드 삭제 — ItemManager·ItemDataAsset·FurnitureManager` | `SerializeActiveItemsToJson`·`GetItemActorByID`·`RepairItem`·Seat/Bed 항등 검사 2줄. `bOccupied` 는 디버그 노출로 유지 | Build |
| C6 | `refactor: VRPawn·Entity 죽은 코드 삭제 — SendNPCDialogue·SetStateTag` | `SendNPCDialogue`(`SayToNpc` 가 대체)·`ICharacterBase::SetStateTag`(구현 클래스 0) | Build |
| C7 | `fix: ExecuteDrop 소음 태그 누락 — NPCActionComponent` | 부록 B1. `NoiseTag_Drop` 전달 | Build |
| C8 | `fix: location_decision 폴백 ID OPTIMAL_0→OPTIMAL — main.py·NPCActionTypes.h` | 부록 B2+B4 | pytest |
| C9 | `refactor: action_failed 엔벨로프 양쪽 삭제 — EnvelopeBuilder·envelope.py·middleware.py·main.py·state.py·tests` | C++: enum 값·`BuildActionFailed`·`RefMsgId` 인자 경로. Python: `EEnvelopeType.ACTION_FAILED`·`ActionFailedPayload`·`parse_action_failed_payload`·`build_failed_event`·`_handle_action_failed`·`_failed_action_history`+락·`_format_failed_history`·`AgentState.failed_action_history`·`test_envelope_middleware` [5]·`test_contract_sync` 대조표. docs `#ai-codebase-guide` 엔벨로프 목록 | Build + pytest |
| C10 | `refactor: TTS 파이프라인 제거 — main.py·npc_audio.py·tts_client.py·TTSService·ASRService` | Python 측 먼저(UE5 가 `npc_audio_response` 를 못 받아도 조용히 무시하므로 순서 안전): `_trigger_dialogue_audio`·`_dispatch_npc_audio`·`_empty_audio_info`·`TTS_ENABLED`·import, `schemas/npc_audio.py`, `clients/tts_client.py`, `TTSService/`·`ASRService/` 디렉터리, `test_ws_roundtrip` 주석, README TTS 절 | pytest |
| C11 | `refactor: 음성 입력·오디오 스트림 컴포넌트 제거 — VoiceInputComponent·NPCAudioStreamComponent·VRPawn·NPCManager·BP_SmartNPC·BP_VRPawn` | **에셋 선행**(에디터 켠 상태, MCP): ① `BP_SmartNPC` 의 `NPCAudioStreamComponent` 제거(`SubobjectDataSubsystem`) + Sample 레벨 NPC 액터(`__ExternalActors__/…/75DR4HX27JJPSDWX81TTA7`) 재저장 ② `IMC_VR` 의 `IA_VoiceInput` 매핑 제거 + `IA_VoiceInput` 에셋 삭제 ③ `BP_VRPawn` 재저장. **그다음 에디터 닫고** C++: `VoiceInputComponent.*`·`NPCAudioStreamComponent.*` 삭제, VRPawn(`VoiceInput`·`VoiceLevelOrb`·`IA_VoiceInput`·`OnVoiceStart/Stop`·`UpdateVoiceIndicator`·`VoiceOrb*` 4개·`HandleVoiceTranscript`→`SayToNpc` 본문으로 흡수), NPCManager(`npc_audio_response` 블록·include), `MCPJsonUtils::ParseNpcAudioResponseFromObject`, `NPCActionKeys::Audio_*` 14개, `OmniAgentConfig::GetTTSPort/RewriteTTSUrl`(+ini 주석), `NPCDialogueUIComponent`(`bWaitForAudio` 인자·`TryBindAudioSubtitle`·Started/Completed 핸들러·`bSubtitleWaitingForAudio`·`bAudioSubtitleBound`), `SmartNPC::ShowSubtitle` 시그니처. `.uasset` 3개 같은 커밋 | Build → 에디터 재기동 → 로드 경고 0 → PIE 채팅 1회(자막·ChatLog) |
| C12 | `docs: 음성 파이프라인 제거 반영 — index.html·Memo·DoList` | `#tts-plan` 섹션 → "폐기(2026-09-12, 복원은 `bd057b8` 이전 이력)" 한 문단으로 축소, `#ai-codebase-guide` 파일 목록(tts_client·npc_audio·VoiceInput·NPCAudioStream 제거), `#integration-guide` TTS 단계 제거, Memo `TTS·ASR 후속` 3건 → Done(폐기)·Handoff `TTS —` 절 삭제, DoList 1-12 "음성 되살릴 때" 항목 삭제, CLAUDE.md §Documentation 의 `#tts-plan` 앵커 언급 제거 | — |

C1~C9 는 에디터 없이 진행 가능. C11 만 에디터 켠 상태에서 에셋 작업 → 닫고 빌드 순서 엄수.

**2026-09-12 실행 완료** — 12 커밋(`f485ad0`…`d752664`) + `bff6a5d`(BP 재저장). C11 은 2차 에디터 패스가 필요했다: C++ 삭제 후 첫 로드에서 `Unable to load VoiceInput … class (VoiceInputComponent) does not exist` 경고 1건 → `BP_VRPawn`·`BP_SmartNPC` 강제 재저장으로 CDO 잔여 서브오브젝트 제거. 네이티브 컴포넌트 클래스를 지울 땐 이 2차 재저장을 계획에 넣을 것.

### 6.4 SPEC_refactor_readability 와의 관계

R1 = C1, R2 = 음성 삭제로 소멸, R3 = 2단계 이후 선택. 해당 문서는 본 절로 대체됨(문서 상단에 표기).

## 부록 A — 죽은 코드 목록 (전부 호출처 0 확인)

**C++ 함수·메서드**
`UNPCStateComponent::TryReflexAction` · `UNPCStateComponent::GetOwnerAIController` · `ASmartNPC::SetBlackboardBool` · `ASmartNPC::GetNPCManager`(래퍼) · `UNPCActionComponent::BaseSendEventToActor` · `BaseDetectEntityInRange` · `BaseEmote`·`BaseDance`·`BaseSing`·`BaseSignalAllies`(인라인 가능) · `ASmartNPCAIController::HandleActionStarted`(+`OnActionStarted` 델리게이트) · `AVRPawn::SendNPCDialogue`(`SayToNpc` 와 중복) · `UPlayerHUDWidget::GetCurrentHealth`·`GetMaxHealth`·`GetHealthPercent`·`GetCurrentStamina`·`GetMaxStamina`·`GetStaminaPercent` · `UMCPJsonUtils::ParseModeActionRequest`·`ParseLocationDecisionResult`·`ParseAffinityUpdate`(문자열판) · `TryParseBehaviorMode` · `FEnvelopeBuilder::BuildActionFailed` · `UItemManager::SerializeActiveItemsToJson`·`GetItemActorByID` · `FItemData::RepairItem` · `UMCPMathUtils::ConvertToUnrealLocation`(클래스 통째) · `UDiceSystem::RollD100`(TryReflexAction 삭제 후) · `ICharacterBase::SetStateTag`

**C++ 클래스·구조체**
~~`FSTEvaluator_NPCState`(+InstanceData)~~(ST_NPC 에셋이 사용, 2026-09-12 정정) · `FEntityState` · `UNPCMap`(흡수) · `UNetworkClientBase`(제거) · `UMCPMathUtils`

**C++ 필드·상수**
`UNPCDialogueUIComponent::bSubtitleWaitingForAudio`(쓰기 전용) · `UItemManager::GlobalItemDataTable`(설정 불가) · `AFurnitureActor::bOccupied`(미러, 선택) · `NPCActionKeys::Media_Track`·`Media_Death`·`Audio_Gesture`·`Audio_LookAtPlayer`·`Audio_StreamMode`·`Audio_RequestId` · `EAttackType` 인자(`ExecuteAttackAction`) · `FurnitureManager` 의 Seat/Bed 항등 검사 2줄

**Python 함수·클래스·모듈**
`config/world_constants.py`(모듈) · `SharedData/world_constants.json` · `schemas.actions.RejectResult` · `schemas.envelope.EQSQueryResult` · `schemas.game_state.Entity` · `PerceptionData.in_line_of_sight`·`activity_context` · `AgentState.behavior_mode`·`facial_state`·`messages` · `tts_client.aclose` · `voice_resolver.resolve_voice` · `llm_factory.get_llm` 의 `OLLAMA_MODELS` set(중복) · rules 의 `"Heal"` 규칙 2건 · `interface_input` 폴백 리스트의 `merchant/blacksmith` · tests 의 `run_all_tests()` 2건, `sys.path` 삽입 4건

**배선 미완(3-6 표) — 결정 대기**

## 부록 B — 발견한 버그 (가독성 조사 중 부수적으로)

1. `UNPCActionComponent::ExecuteDrop` 이 `ReportNoiseEvent` 에 태그를 안 넘겨 반사 룰 "Drop" 이 NPC 드랍에는 절대 안 걸립니다. `NoiseTag_Drop` 을 넘기면 끝.
2. `main.py::_location_decision_fast_path` 의 폴백 ID `"OPTIMAL_0"` — C++ 후보 ID 는 `"OPTIMAL"`. 지금은 C++ 의 "모르는 ID → 첫 항목" 폴백으로 우연히 동작.
3. `UItemManager::GetItemDataByID` 가 매 호출 `StaticLoadObject` — `GlobalItemDataTable` 이 설정 불가라 항상 폴백 경로. 성능 문제.
4. `FLocationCandidate::CandidateId` 주석 `"SAFE_0"` — 실제 `"SAFE"`. 문서 드리프트.
5. `CognitiveEngine/README.md` 가 여전히 "SLM Reflex" 를 서술 — W34 에 C++ 테이블로 대체됨.
