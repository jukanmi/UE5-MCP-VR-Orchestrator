# SPEC: item-collision-gen — 아이템 충돌 모양 자동 생성

> 상태: **완료(2026-10-03)** — M1~M3 적용, 헤드셋 확인(쥐기·던지기 회귀 없음, 컵·양동이 안쪽 손가락, 바닥 접지) 완료. — 브랜치 `feat/reality_grab` 계속(Core/Physics 신규 파일이라 VRPawn 미커밋과 안 겹침).
> 후속: NPC active ragdoll(손이 닿은 부위만 밀림)의 Physics Asset 바디 생성이 이 SPEC 의 맞춤 코어를 재사용한다.

## 목표

아이템 StaticMesh 마다 **물리 비용이 가장 적으면서 모양에 맞는** 단순 충돌을 자동으로 만든다.

- ~~현재는 임포트 때 18-DOP 헐 1개만 붙인다~~ → 2026-10-02 실측: 아이템 71종 대부분이 이미 자동 분해 헐 8개(합계 428헐), 1개짜리는 11종, WaterBucket 20. 그래서 오목한 물건(컵·양동이·바구니·활·칼 가드)은 속이 찬 뚱뚱한 껍질이 된다.
  - 손가락이 안쪽이나 손잡이 사이로 못 들어간다.
  - 물건이 실제 모양보다 먼저 닿는다.
- 같은 맞춤 코어를 NPC 뼈 캡슐 맞춤(`UNPCBoneCapsuleSet::FillFromMeshes`)과 공유해, 이후 NPC Physics Asset 생성이 그 위에 올라가게 한다.

## 범위 (변경 파일·시스템)

| 층 | 대상 | 변경 |
|---|---|---|
| C++ 신규 | `Core/Physics/` 충돌 맞춤 코어 | 정점 구름 → 상자·구·캡슐 맞춤 + 부피 오차 측정. 백분위 맞춤은 `NPCBoneCapsuleSet.cpp` 의 것을 옮겨 공용화 |
| C++ 신규 | 아이템 충돌 생성 에디터 함수 | StaticMesh 를 받아 `BodySetup` AggGeom 을 교체. CallInEditor + MCP(`ue_call_function`) 호출 |
| C++ | `Core/Physics/NPCBoneCapsuleSet.cpp` | 내부 백분위 계산을 공용 코어 호출로 교체. 결과 값 불변 |
| 도구 | `tools/import_meshes_to_ue.py` | M1·M2 동안 NDOP18 유지, M3 에서 새 함수 호출로 교체 |
| 에셋 | `/Game/Core/Mesh/Items/` StaticMesh 약 72종 | 충돌 재생성 |

건드리지 않는 것:
- 아이템 질량: `DT_ItemRegistry.Weight` 로 덮어쓰는 `SetMassOverrideInKg` 가 이미 있음.
- 손가락 캡슐: 매 프레임 재용접·GJK 비용 때문에 캡슐 유지(2026-10-02 판단).
- NPC Physics Asset: 후속 SPEC.

## 결정 사항

- **전략 = 기본도형 자동 선택 + 컨벡스 분해 폴백.**
  - 상자·구·캡슐 각각을 정점에 맞춘 뒤, 메시 부피 대비 오차가 가장 작은 하나를 고른다.
  - 그 오차가 임계값을 넘으면(오목·복합 형상) 컨벡스 분해로 넘긴다. 분해는 헐 수와 헐당 정점 수 상한을 둔다.
  - 이유: 기본도형 1개가 Chaos 접촉 비용이 가장 싸고, 쥐었을 때 떨림이 적다.
- **맞춤 = 백분위.** 축 방향 범위와 반지름을 최대값이 아닌 백분위로 잡아 튀는 정점(장식·이펙트 조각)에 끌리지 않게 한다. 뼈 캡슐 맞춤과 같은 원리이고 같은 코드다.
- **구현 = C++ 에디터 함수.** `FillFromMeshes` 와 같은 방식으로, 에셋 Details 버튼과 MCP 로 부른다. BodySetup·PA 바디 편집은 Python API 가 빈약해서 C++ 로 한다.
- **분해·오목 판정 = V-HACD 2.3 직접 호출** (엔진 ThirdParty `VHACD` 모듈, 에디터 전용 의존).
  - 엔진 래퍼 `DecomposeMeshToHulls`(PhysicsUtilities `ConvexDecompTool.cpp`)는 `m_concavity = 0` 고정이라 오목하든 말든 요청 헐 수까지 쪼갠다 → 볼록 판정에 못 쓴다.
  - 직접 호출해 `m_concavity > 0` 을 주면 볼록한 물건은 헐 1개에서 멈춘다. **헐 수 = 오목 판정**, 헐 `m_volume` 합 = 기준 부피. 복셀화 기반이라 Hunyuan3D 의 비수밀 메시에서도 부피를 직접 적분하지 않아도 된다.
  - 흐름: V-HACD → 헐 1개이거나 **헐 부피 합 / 전체 정점 헐 부피 ≥ 0.5**(속이 안 빈 물건, `MinSolidRatio`)면 상자·구·캡슐을 맞춰 오차가 가장 작은 것을 쓰고(상한 0.5), 아니면 헐 그대로. 기본도형 우선(2026-10-02 사용자 결정): 병 목·두루마리 끝처럼 오목으로 잡혀도 쥐고 던지기엔 기본도형으로 충분.
  - 값: `m_concavity` **0.02**(V-HACD 기본 0.0025 는 울퉁불퉁한 돌도 쪼갬, 0.05 는 와인컵 속이 참 — 아래 M2 실측)·헐 상한 16·헐당 정점 32·기본도형 오차 상한 0.5. 기본도형 오차 = 부피 오차와 주축별 크기 오차 중 큰 것(부피만 보면 납작한 빵에 캡슐이 골라져 반지름만큼 떠 있음).
  - 테두리·손잡이 정밀도가 모자라면 CoACD 로 굽는 방식을 후속으로 검토한다.
- **결과 기록**: 메시마다 선택한 모양·오차·헐 수를 로그로 남겨, 일괄 실행 뒤 표로 확인한다.

## 완료 기준

- 72종 일괄 실행 후 메시마다 선택된 모양과 오차가 로그 표로 나온다. 볼록한 물건(돌·두루마리·약병)은 기본도형 1개가 되고, 오목한 물건(컵·양동이)은 분해된다.
- 헤드셋 없는 PIE:
  - 모든 아이템이 바닥을 통과하지 않는다.
  - 떨어뜨려도 튀거나 떨지 않는다.
  - 쥐기·던지기가 회귀하지 않는다.
- WaterBucket: 지금 20헐 수준(벽 48방향 폐쇄·속 비움)보다 나빠지지 않는다. 나빠지면 보호 대상으로 둔다(미결 1).
- `NPCBoneCapsuleSet::FillFromMeshes` 결과가 공용 코어로 바꾼 뒤에도 같다(4개 DataAsset 재실행 diff 0).
- 헤드셋: 컵·양동이 안쪽과 손잡이 사이로 손가락이 들어간다(사용자 확인, DoList 등록).

## 단계

- **M1 코어** ✅: 맞춤 코어 추출(`Core/Physics/CollisionFit`) + `NPCBoneCapsuleSet` 교체(DA 4개 결과 float 완전 일치).
- **M2 아이템 함수**: 기본도형 선택 + 분해 폴백 + 로그 표. 몇 종으로 시험한 뒤 임계값을 정한다.
- **M3 일괄 적용** ✅: 72종 재생성, PIE 회귀 확인, 임포트 도구 연결.

## M3 결과 (2026-10-03, 외부 에셋 메시 72종)

- 새 메시는 72종 중 68종이 단순 충돌 0개(물리 바디 없음), 4종은 임포트 자동 헐 1개 — 튜닝된 적 없어 지우고 생성(무게중심 보정 전부 0).
- 결과: 상자 12·캡슐 16·헐 44(합 317헐 + 기본도형 28). 옛 Hunyuan 메시 428헐보다 적다.
- 추가 규칙(M3 중 발견):
  - 틀 후보 = 메시 로컬 축 + 관성 주축. 주축만 쓰면 폭≈높이인 TarBucket 에서 축이 기울어 캡슐이 위로 19%(5cm) 튀어나왔다.
  - 크기 오차 = 도형의 로컬 축 범위가 메시 백분위 범위에서 튀어나오거나 모자란 정도 / 축 크기, 상한 0.15(`MaxExtentError`). 부피 오차와 같은 상한(0.5)이면 돌출을 못 막는다.
  - 충돌이 없던 메시는 무게중심 보정 0(전체 정점 헐 무게중심은 망원경 10cm·검 7cm 빈 공간으로 감).
- 속 비움(위→아래 라인트레이스 깊이 / 높이): WaterBucket 0.78~0.87(옛 수동 20헐 0.91), WineCup 0.26~0.39(잔 부분), HerbBasket 0.15~0.85.
- PIE(헤드셋 없음, 90fps, 퀘스트 아이템 10종 제외 62종을 바닥 60cm 위에서 낙하): 관통 0, 10초 뒤 깨어 있는 것 2(KnightSword·Microphone, 이웃 아이템에 기대 20°/s).
- **30fps 관통**: 같은 시험을 30fps(에디터 백그라운드 스로틀)로 하면 16종이 바닥 통과 — 닿는 프레임 이동 11cm. 아이템 CCD 가 꺼져 있고, 켜면 30fps 에서도 0. 90fps 라도 10m/s 던지기면 같은 이동량이라 `ADroppedItemBase::BeginPlay` 에서 CCD 를 켰다(2026-10-03 사용자 결정) — 백그라운드 8fps 낙하에서도 16종 관통 0.
- 헤드셋 확인(2026-10-03 사용자): 쥐기·던지기 회귀 없음, WaterBucket·WineCup 안쪽·손잡이 사이로 손가락 들어감, 떨어뜨린 아이템이 뜨거나 묻히지 않음.

## M2 실측 (2026-10-02)

구현: `UItemCollisionGen::GenerateItemCollision`(`Core/Physics/ItemCollisionGen`), Python `unreal.ItemCollisionGen.generate_item_collision(mesh)`. 메시당 0.0~1.1초. 시험은 메모리에서만 하고 에셋은 git 으로 원복.

- **오목 판정**: 위에서 아래로 단순 충돌 라인트레이스 깊이(높이 대비). WaterBucket(수동 20헐) 0.91. WineCup 은 concavity 0.05 → 0 (속 참), 0.02 → 0.51~0.93 (속 빔).
- **72종 결과(0.02)**: 상자 7(Bandage·Bread·HolyBook·IronIngot·Plank·ShipBoard·SoftBlanket), 캡슐 3(Antidote·CrystalBall·Rope), 헐 1개 1(Rock — 캡슐 오차 0.56), 다중 헐 60, 건너뜀 1(WaterBucket). **헐 합계 428 → 631(+47%)**, 16종이 상한 16.
- **SPEC 기대와 다른 점**: 약병(HealthPotion 7·AlchemistryVial 5)·두루마리(Ancient 16·Oath 16·Prophecy 14)·SmokeBomb 16·MagicGem 16 이 기본도형이 아닌 다중 헐. 컵 속을 비우는 concavity 가 병 목·두루마리 끝도 오목으로 본다.
- **기본도형 우선 반영 후(24종 복제본 시험)**: ManaPotion·SpellScroll·Lantern·HerbBasket·CoinPouch·WaterSkin·IronOre → 캡슐, OathScroll·Whetstone → 상자. WineCup(0.07)·활(0.25)·검(0.41)·ProphecyScroll(0.18)은 헐 유지. HealthPotion·AlchemistryVial 은 캡슐 오차 1.27·0.96 으로 헐 유지 — 메시 문제로 추정, 깔끔한 메시 편입(사용자 진행 중) 뒤 재확인. HerbBasket 캡슐은 무게중심 보정 7.8cm.
- **무게중심**: 기준을 '전체 정점 헐'로 하면 활 17cm 가 빈 공간으로 감 → '바꾸기 전 충돌의 무게중심 + 기존 보정값'으로 변경(재실행해도 유지). 보정은 대부분 1cm 이하, 최대 ShipWheel 2.3·WineCup 1.8·HuntingBow 1.8cm.

## 결정된 미결 (2026-10-02)

1. **수동 충돌 보호 = 제외 목록.** 함수 인자로 제외 메시 이름 배열(기본에 `WaterBucket`). 새로 손으로 다듬은 메시는 목록에 추가.
2. **임계값 = V-HACD 헐 수로 오목 판정** (위 결정 사항). 수치는 M2 시험에서 확정.
3. **무게중심 = 원래 위치 유지.** 기준 = 바꾸기 전 충돌의 무게중심(+ 기존 보정값). 차이를 BodySetup `DefaultInstance.COMNudge` 에 저장. StaticMeshComponent 는 이 값을 안 읽어서(엔진 확인: DefaultInstance 는 충돌 프로필만 복사) `ADroppedItemBase::BeginPlay` 가 `SetCenterOfMass` 로 적용. 관성은 새 도형을 따른다.
4. **임포트 도구 연결 = M3.** M1·M2 동안 NDOP18 유지.
5. **기본도형 조합 = 안 둠.** M2 에서 헐이 많이 나오는 물건이 있으면 재검토.
