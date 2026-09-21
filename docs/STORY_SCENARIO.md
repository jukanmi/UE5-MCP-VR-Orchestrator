# 스토리 시나리오 초안 (Story Scenario Draft)

> **프로젝트**: UE5_MCP_VR (OmniAgent Orchestrator)  
> **시스템 연동**: [SPEC_story_director.md](SPEC_story_director.md) (`app/story/content/main.yaml` 비트 시트 매핑 전제)  
> **작성일**: 2026-09-18  

---

## 1. 등장인물 프로필 및 역할 정의

| 캐릭터 ID | 성격 및 포지션 | 상세 설정 및 게임 내 역할 |
| :--- | :--- | :--- |
| **Player** | **초보 기사 (주인공)** | 갓 기사단에 입단했으나 왕국 멸망의 날 스승의 희생으로 겨우 살아남음. 스승의 유품인 '부서진 성검'을 쥐고 마왕 타도를 결의함. |
| **James** | **오더를 내리는 도적 (전술가)** | 뒷세계에 능통하고 냉철한 정보통. "내가 시키는 대로만 움직여"라며 플레이어와 파티의 행선지 및 전술 목표(오더)를 지정해 주는 실질적 전략 리더. |
| **Skadi** | **화끈한 거너 (화력 담당)** | 앞뒤 안 가리고 대형 총포를 난사하는 호탕하고 화끈한 성격. 말보다 방아쇠가 먼저 나가지만, 동료를 건드리는 자는 절대 용서하지 않는 의리파. |
| **Moca** | **조용한 마법사 (비전 연구가 & 첩자)** | 나직하고 차분한 어조(ASMR 톤)의 비전 마법사. 성검을 복원하는 핵심 조력자이지만, 실은 마왕군에게 걸린 치명적인 저주와 인질 때문에 어쩔 수 없이 첩자 노릇을 하고 있음. 내면에서 깊은 죄책감과 고뇌를 겪음. |
| **Elara** | **기사단장 (영웅의 상징)** | 왕국 기사단의 최고 지휘관. 왕국 함락 당시 전사한 줄 알았으나 중반에 잔존 저항군을 이끌며 생존 확인, 파티의 든든한 정신적 지주로 합류. |
| **Guard** | **성문 경비병 (수문장)** | 폐허가 된 성문/피난처를 끝까지 사수하는 충직한 문지기. 플레이어에게 초기 위험을 경고하고 튜토리얼성 정보를 전달. |

---

## 2. 메인 스토리 5막 구조 (Plot Breakdown)

```
[1막: 도입] ────────> [2막: 전개] ────────> [3막: 위기] ────────> [4막: 절정] ────────> [5막: 결말]
 멸망과 각성         동료 규합과 성장        Moca의 배신          마왕성 결전          참회와 새 여정
 (성검 유품 획득)     (Moca/Skadi/Elara)   (성검 탈취/패배)    (Moca구출/성검각성)   (용서와 모험)
```

---

### [1막: 도입] 멸망과 각성 (The Fall & Awakening)

* **배경**: 번영하던 성채 왕국이 어둠의 마왕군에 의해 하룻밤 만에 함락된다.
* **전개**:
  1. 기습 침공으로 성이 불타오르고, 스승은 주인공(플레이어)을 성 밖으로 탈출시키며 장렬히 산화한다.
  2. 스승이 마지막으로 남긴 유품은 마력이 꺼진 채 날이 부러진 **'부서진 성검'**.
  3. 성문 경비병(`Guard`)의 도움으로 피난처에 도착한 주인공은 스승의 복수와 왕국 탈환을 다짐한다.
  4. 이때 어둠 속에서 나타난 도적 `James`가 주인공의 성검을 알아보고 말을 건넨다. *"그 칼, 평범한 쇳덩이가 아니군. 살아남고 싶으면 내 오더를 따라."*

---

### [2막: 전개] 동료 규합과 성장 (Gathering the Party)

* **배경**: 마왕군이 왕국 전역에 전초기지를 세우고 자원을 약탈 중이다. 성검을 복구하려면 흩어진 성검 조각들과 전문가들이 필요하다.
* **전개**:
  1. **조용한 마법사 Moca 영입**: 고대 도서관 폐허에서 조용히 마도서를 읽고 있던 `Moca`를 구출. Moca는 성검을 유심히 분석한 뒤 *"조용히 귀 기울여 보세요... 검의 파편들이 마왕군 간부들의 손에 흩어져 있어요"*라며 복원법을 알려준다.
  2. **화끈한 거너 Skadi 영입**: 마왕군 보급로를 혼자서 폭격하며 난전을 벌이던 `Skadi`와 조우. Skadi는 *"하하! 화끈하게 마왕 놈들 대가리를 부술 파티라고? 나도 끼워줘!"*라며 합류.
  3. **첫 번째 간부 격파 & 성검 1차 복구**: James의 작전 지휘 하에 전초기지 간부를 격파하고 첫 번째 성검 조각 획득. Moca의 정교한 마법으로 검날의 절반이 복구됨.
  4. **기사단장 Elara의 합류**: 마왕군 포위망에 갇혀 있던 기사단장 `Elara`를 구출. Elara는 살아남은 주인공의 성장과 부서진 성검을 보고 눈시울을 붉히며 파티의 정식 일원으로 검을 맹세한다.

---

### [3막: 위기] 배신과 좌절 — Moca의 눈물 (Betrayal & Fall)

* **배경**: 마지막 성검 조각을 손에 넣고 완전한 복원을 눈앞에 둔 은신처.
* **전개**:
  1. **충격의 배신**: 성검의 마지막 결합식을 진행하던 순간, `Moca`가 갑자기 마왕군 공간 전송진을 발동시킨다. 동시에 마왕군 정예 부대가 은신처를 포위한다.
  2. Moca는 떨리는 손으로 완성 직전의 성검을 품에 안고 눈물을 글썽인다. *"미안해요... 이러지 않으면... 제 일족이 전부 몰살당해요... 부디 절 용서하지 마세요..."*
  3. Moca는 성검을 쥔 채 마왕군 간부와 함께 어둠 속으로 사라지고, 일행은 마왕군의 기습 폭격에 큰 부상을 입고 뿔뿔이 흩어진다.
  4. **재결속**: 피투성이가 된 주인공이 절망하여 무릎을 꿇고 있을 때, `James`가 다가와 멱살을 잡아채며 일침을 놓는다. *"칼 뺏겼다고 네 기사도까지 뺏겼냐? 그 녀석 울고 있었어. 억지로 끌려간 거 모르면 넌 멍청이다. 내 오더 들어. 성검도 되찾고 Moca 녀석도 멱살 잡고 데려온다."*
  5. `Elara`의 굳건한 신념과 `Skadi`의 분노 어린 투지가 합쳐져 파티는 마왕성으로 향할 결의를 다진다.

---

### [4막: 절정] 마왕성 결전과 각성 (Citadel Assault & Redemption)

* **배경**: 암흑의 기운이 소용돌이치는 마왕의 본거지.
* **전개**:
  1. **마왕성 침투**: James의 비밀 루트와 Skadi의 전면 양동 포격으로 마왕성에 돌파 성공.
  2. **Moca 구출과 화해**: 마왕성에 진입한 일행은, 성검을 바쳤음에도 토사구팽당해 마력 추출 결계에 묶여 처형당할 위기에 처한 Moca를 발견한다.
  3. Skadi와 Elara가 결계를 부수고 Moca를 구출한다. Moca는 피눈물을 흘리며 주인공 앞에 엎드린다. *"날 죽여도 좋아요... 하지만 제발 저 마왕을..."* 주인공은 묵묵히 손을 내밀어 Moca를 일으켜 세운다.
  4. **마왕과의 사투 & 성검의 파괴 위기**: 성검을 되찾아 마왕과 결전을 벌이지만, 마왕의 압도적인 암흑 투기에 성검이 다시 산산조각 날 위기에 처한다.
  5. **진정한 빛의 검 각성**:
     * Moca가 자신의 모든 생명력과 비전 마력을 성검 파편에 쏟아붓기 시작한다. *"이번엔... 도망치지 않겠어요!"*
     * Elara의 충성심, Skadi의 불꽃같은 투지, James의 집념, 그리고 주인공의 꺾이지 않는 용기가 하나로 공명한다.
     * 부서진 파편들이 눈부신 순백의 빛으로 연결되며 **'진정한 빛의 성검'**으로 최종 각성!
     * 주인공의 일격에 마왕군 군주가 비명을 지르며 영구 소멸한다.

---

### [5막: 결말] 평화와 새로운 모험 (Peace & Beyond)

* **배경**: 마왕군이 물러가고 햇살이 찬란하게 쏟아지는 왕국 광장.
* **전개**:
  1. 성문 경비병(`Guard`)과 왕국 유민들이 영웅들을 환호성으로 맞이한다.
  2. 왕국 수호의 공로로 주인공에게 최고위 관직과 영지가 제안되지만, 주인공은 성검을 왕국 제단에 안치하고 정중히 거절한다.
  3. `Elara`는 잿더미가 된 왕국을 재건하기 위해 기사단 총사령관으로 성에 남는다.
  4. 한편, 죄책감에 조용히 짐을 싸서 떠나려던 `Moca`의 앞을 `James`와 `Skadi`가 가로막는다.
     * James: *"어딜 혼자 도망가냐? 우리 배신한 빚은 평생 우리 파티 마법사로 일하면서 갚는 거다. 알겠어?"*
     * Skadi: *"하하! 너 없으면 내 대포에 비전 마법 인챈트는 누가 해주냐고! 어서 짐 챙겨!"*
     * Moca: 눈물을 훔치며 엷은 미소를 짓는다. *"...네, 평생 갚을게요... 여러분 곁에서..."*
  5. 주인공, James, Skadi, Moca 4인은 바다 너머 미지의 신대륙을 향해 새로운 모험을 떠난다.

---

## 3. Story Director 연동 규격 (`app/story/content/main.yaml` 초안)

`SPEC_story_director.md` 규격에 맞춘 실제 시스템 주입용 비트 시트 구조입니다:

```yaml
title: "부서진 성검의 맹세"
start: "b1_kingdom_fall"

beats:
  # ──────────────────────────────────────────────────────────
  # 1막: 도입
  # ──────────────────────────────────────────────────────────
  - id: "b1_kingdom_fall"
    title: "성문 앞의 피신"
    summary: "왕국이 기습으로 불타고 스승을 잃은 주인공이 성문에 도착했다."
    npc_goals:
      Guard: "플레이어의 부상을 확인하고 성문 안 임시 거처로 안내한다"
    quest_log: "불타는 성문을 지나 경비병에게 상황을 확인하자"
    complete_when:
      type: "talked_to"
      npc_id: "Guard"
      min_turns: 1
    next: "b2_james_order"
    unlocks_side: ["s_hunt_forest_raiders"]

  - id: "b2_james_order"
    title: "도적의 오더"
    summary: "James가 부서진 성검의 가치를 알아보고 동료 규합을 제안한다."
    npc_goals:
      James: "성검의 상태를 지적하며 숲의 마법사 Moca와 화력 담당 Skadi를 찾으라고 오더를 내린다"
    quest_log: "James의 조언을 듣고 은신처의 동료들을 찾으러 가자"
    complete_when:
      type: "talked_to"
      npc_id: "James"
      min_turns: 2
    next: "b3_recruit_party"
    unlocks_side: ["s_moca_herbs"]

  # ──────────────────────────────────────────────────────────
  # 2막: 전개
  # ──────────────────────────────────────────────────────────
  - id: "b3_recruit_party"
    title: "마법사와 거너"
    summary: "조용한 마법사 Moca와 화끈한 거너 Skadi가 합류한다."
    npc_goals:
      Moca: "조용한 목소리로 성검 조각의 마력 반응 위치를 플레이어에게 알려준다"
      Skadi: "마왕군 놈들을 날려버릴 준비가 됐다며 화끈하게 무기를 점검한다"
    quest_log: "Moca와 Skadi를 파티에 영입하고 성검 복원 단서를 찾자"
    complete_when:
      type: "talked_to"
      npc_id: "Moca"
      min_turns: 2
    next: "b4_reforge_and_elara"
    unlocks_side: ["s_hunt_bridge_troll"]

  - id: "b4_reforge_and_elara"
    title: "기사단장의 생환"
    summary: "마왕군 간부를 처치하고 생존한 기사단장 Elara를 구출한다."
    npc_goals:
      Elara: "살아남은 주인공을 격려하며 기사단의 깃발을 걸고 합류를 선언한다"
      James: "기사단장까지 모였으니 마왕군 요새 공략 플랜을 브리핑한다"
      Moca: "조용히 성검을 1차 복구하지만 어딘가 불안하고 초조한 기색을 보인다"
    quest_log: "기사단장 Elara와 합류하여 성검 1차 복원을 마치자"
    complete_when:
      type: "boss_killed"
      boss_id: "Commander_Vorg"
    next: "b5_moca_betrayal"
    unlocks_side: ["s_hunt_dead_wraith"]

  # ──────────────────────────────────────────────────────────
  # 3막: 위기 — Moca의 배신
  # ──────────────────────────────────────────────────────────
  - id: "b5_moca_betrayal"
    title: "빼앗긴 성검과 Moca의 눈물"
    summary: "Moca가 성검을 마왕군에 넘기고 사라지지만, James가 진실을 꿰뚫어보고 마왕성 잠입을 지시한다."
    npc_goals:
      James: "절망한 플레이어의 멱살을 잡고 Moca의 눈물에 담긴 진실과 마왕성 침투 작전을 오더한다"
      Elara: "스승의 검이 없어도 기사의 의지는 꺾이지 않는다고 플레이어에게 용기를 준다"
      Skadi: "Moca를 협박한 마왕 놈들을 가루로 만들어 버리겠다고 화력 지원을 맹세한다"
    quest_log: "Moca가 성검을 들고 사라졌다. James의 오더를 따라 마왕성으로 향하자"
    complete_when:
      type: "talked_to"
      npc_id: "James"
      min_turns: 2
    next: "b6_save_moca_and_boss"

  # ──────────────────────────────────────────────────────────
  # 4막: 절정 — 마왕성 결전
  # ──────────────────────────────────────────────────────────
  - id: "b6_save_moca_and_boss"
    title: "마왕성 결전과 각성"
    summary: "마왕성에 갇힌 Moca를 구출하고, 동료들의 염원과 Moca의 마력으로 성검을 빛으로 각성시켜 마왕을 처단한다."
    npc_goals:
      Moca: "눈물로 참회하며 성검 파편에 자신의 모든 비전 마력을 주입해 진정한 빛으로 각성시킨다"
      Skadi: "퇴로를 열기 위해 마왕군 수호병들에게 전면 포격을 퍼붓는다"
      Elara: "마왕의 친위대를 막아서며 플레이어의 결정타 각을 만든다"
    quest_log: "Moca를 구출하고 각성한 빛의 성검으로 마왕을 처치하자"
    complete_when:
      type: "boss_killed"
      boss_id: "DemonLord"
    next: "b7_epilogue"

  # ──────────────────────────────────────────────────────────
  # 5막: 결말
  # ──────────────────────────────────────────────────────────
  - id: "b7_epilogue"
    title: "새로운 여정"
    summary: "마왕이 소멸하고 왕국이 재건된다. Moca는 용서받고 4인은 새로운 모험을 떠난다."
    npc_goals:
      Guard: "영웅이 되어 돌아온 일행을 성문에서 눈물로 맞이한다"
      Elara: "왕국 재건을 위해 남으며 주인공 일행의 앞날에 신의 가호를 빈다"
      James: "Moca에게 평생 마법 서포트로 빚을 갚으라 츤데레 오더를 내리며 출항을 재촉한다"
    quest_log: "재건된 왕국을 뒤로하고 동료들과 새로운 모험을 떠나자"
    complete_when:
      type: "talked_to"
      npc_id: "Elara"
      min_turns: 1
    next: "end"
```

---

## 4. 서브퀘스트 명세 (Side Quests / Guild Bounties)

메인 퀘스트 진행도에 따라 해금되며, 500m×500m 레벨의 주요 지형(숲, 강 다리, 죽은 숲 등)을 탐험하도록 유도하는 서브 의뢰 4종입니다.

| 서브퀘스트 ID | 제목 | 의뢰주 | 해금 비트 | 수행 장소 | 목표 및 보상 |
| :--- | :--- | :---: | :---: | :--- | :--- |
| **`s_hunt_forest_raiders`** | **숲의 약탈자 척살령** | `Guard` | `b1` 이후 | 남쪽 숲길 외곽 (`FOREST`) | 도적 3명 처치<br>보상: `CoinPouch`, `Bandage` 2개 |
| **`s_moca_herbs`** | **마법사의 비약: 달빛 약초** | `Moca` | `b2` 이후 | 숲속 모닥불 빈터 (`CLEARING`) | 약초 바구니(`HerbBasket`) 수거 후 전달<br>보상: `HealthPotion`, Moca 친밀도 |
| **`s_hunt_bridge_troll`** | **현상 수배: 다리의 도살자** | `James` | `b3` 이후 | 큰 강 다리 교각 (`BRIDGE`) | 네임드 오크 바그론 처치<br>보상: `IronIngot`, `RumBottle` |
| **`s_hunt_dead_wraith`** | **죽은 숲의 원혼 정화** | `Elara` | `b4` 이후 | 마왕성 앞 죽은 숲 (`DEAD_FOREST`) | 전사한 기사의 망령 성불 및 유품 회수<br>보상: `OathScroll`, `HolyWater` |

---

### 4.1 서브퀘스트 YAML 파일 규격 (`app/story/content/side/`)

#### ① `content/side/hunt_forest_raiders.yaml` (초반 토벌)
```yaml
id: "s_hunt_forest_raiders"
title: "길드 토벌: 숲의 약탈자 척살령"
available_after: "b1_kingdom_fall"
giver: "Guard"
summary: "왕국 멸망의 혼란을 틈타 남쪽 숲길에서 피난민들을 약탈하는 도적 떼를 소탕하라."

npc_goals:
  Guard: "피난민들의 식량을 털어먹는 파렴치한 놈들이 남쪽 숲에 숨어 있다고 분노하며 토벌을 요청한다"

quest_log: "남쪽 숲 외곽에 숨어 있는 약탈자 도적들을 소탕하자"

complete_when:
  type: "enemy_killed"
  enemy_id: "Bandit_Raider"
  count: 3

rewards:
  items: ["CoinPouch", "Bandage"]
  flags: ["forest_safe"]
```

#### ② `content/side/moca_herbs.yaml` (수색 및 채집)
```yaml
id: "s_moca_herbs"
title: "마법사의 비약: 달빛 약초 수거"
available_after: "b2_james_order"
giver: "Moca"
summary: "성검의 마력 결함을 보완하고 상처를 치료하기 위해 숲속 빈터의 달빛 약초 바구니를 가져오라."

npc_goals:
  Moca: "숲속 모닥불 근처 바위 틈에 놓아둔 달빛 약초 바구니를 조심스럽게 회수해 달라고 부탁한다"

quest_log: "남쪽 숲 모닥불 빈터(Clearing)에서 약초 바구니(HerbBasket)를 찾아 Moca에게 건네자"

complete_when:
  type: "item_acquired"
  item_id: "HerbBasket"

rewards:
  items: ["HealthPotion", "ManaPotion"]
  flags: ["moca_herbs_delivered"]
```

#### ③ `content/side/hunt_bridge_troll.yaml` (현상 수배 네임드 토벌)
```yaml
id: "s_hunt_bridge_troll"
title: "현상 수배: 다리의 도살자"
available_after: "b3_recruit_party"
giver: "James"
summary: "강 다리를 봉쇄하고 통행인들을 도살하는 거대 오크 바그론을 처치하여 전초기지 진입로를 확보하라."

npc_goals:
  James: "바그론의 정면은 단단하니 다리 난간에서 기습하라는 냉철한 전술 오더를 내린다"
  Skadi: "다리 밑 괴물 놈에게 새로 정비한 대포 맛을 보여주자며 신나게 웃는다"

quest_log: "강 다리(Bridge) 교각 아래에 도사린 오크 바그론을 처치하자"

complete_when:
  type: "boss_killed"
  boss_id: "Orc_Vagron"

rewards:
  items: ["IronIngot", "RumBottle"]
  flags: ["bridge_cleared"]
```

#### ④ `content/side/hunt_dead_wraith.yaml` (퇴마 및 유품 회수)
```yaml
id: "s_hunt_dead_wraith"
title: "기사단의 안식: 죽은 숲의 원혼"
available_after: "b4_reforge_and_elara"
giver: "Elara"
summary: "마왕성 앞 죽은 숲에서 사령술에 오염되어 배회하는 옛 전우들의 영혼을 성불시키고 서약서를 회수하라."

npc_goals:
  Elara: "타락한 기사단 전우들이 명예롭게 안식할 수 있도록 원혼을 거두어 달라고 비통하게 부탁한다"

quest_log: "마왕성 앞 죽은 숲(Dead Forest)의 망령들을 정화하고 기사단의 유품을 회수하자"

complete_when:
  type: "flag"
  name: "wraiths_purified"

rewards:
  items: ["OathScroll", "HolyWater"]
  flags: ["knights_rested"]
```

