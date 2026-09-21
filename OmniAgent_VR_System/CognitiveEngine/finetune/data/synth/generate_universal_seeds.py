# -*- coding: utf-8 -*-
"""generate_universal_seeds.py

Generates universal synthetic seed data for Stage 1 (e4b Dialogue/Action) 
and Stage 2 (12B Multi-step Plan) fine-tuning datasets across diverse personas, 
environments, and categories.
"""

import os
import yaml

SYNTH_DIR = r'C:\github\UE5_MCP_VR\OmniAgent_VR_System\CognitiveEngine\finetune\data\synth'

DIVERSE_PERSONAS = [
    {"role": "Mage", "desc": "지적이고 신중한 마법 아카데미 학자"},
    {"role": "Blacksmith", "desc": "호탕하고 망치질이 일상인 대장장이"},
    {"role": "Scholar", "desc": "호기심 많은 유적 탐사 학자"},
    {"role": "Assassin", "desc": "냉철하고 은밀한 그림자 암살자"},
    {"role": "Innkeeper", "desc": "친절하고 수다스러운 주점 주인"},
    {"role": "Wanderer", "desc": "자유분방한 방랑 시인"},
    {"role": "Knight", "desc": "규율과 명예를 목숨처럼 여기는 영지 기사"},
    {"role": "Navigator", "desc": "조류와 별자리를 읽는 진중한 항법사"},
    {"role": "Merchant", "desc": "실리적이고 계산이 빠른 외지 상인"},
    {"role": "Alchemist", "desc": "실험과 시약 제조에 몰두하는 연금술사"},
    {"role": "Elara", "desc": "근엄한 왕국 기사단장"},
    {"role": "James", "desc": "해적단 항법사"},
    {"role": "Skadi", "desc": "사나운 해적선장"},
    {"role": "Moca", "desc": "ASMR 스트리머"},
    {"role": "Guard", "desc": "성벽 경비병"}
]

CATEGORIES = ["Lifestyle", "Combat", "Social", "Task", "Investigation", "Common", "Talk", "Reject"]

UNIVERSAL_SCENARIOS = [
    # Lifestyle
    {
        "id": "univ_lifestyle_sit_01",
        "category": "Lifestyle",
        "persona": "Mage",
        "situation": {
            "valid_targets": ["Player", "Self", "Chair_Study_01"],
            "nearby_furniture": [{"id": "Chair_Study_01", "type": "Chair", "occupied": False, "dist": 2}]
        },
        "utterance": "주문서 해독에 집중해야 하니 의자에 앉겠다.",
        "gold": {
            "mode": "Lifestyle",
            "facial": "Neutral",
            "actions": [{"type": "Sit", "target": "Chair_Study_01"}]
        }
    },
    {
        "id": "univ_lifestyle_sleep_01",
        "category": "Lifestyle",
        "persona": "Wanderer",
        "situation": {
            "valid_targets": ["Player", "Self", "Bed_Inn_01"],
            "nearby_furniture": [{"id": "Bed_Inn_01", "type": "Bed", "occupied": False, "dist": 3}]
        },
        "utterance": "긴 여정이었으니 오늘 밤은 침대에서 자야겠군.",
        "gold": {
            "mode": "Lifestyle",
            "facial": "Tired",
            "actions": [{"type": "Sleep", "target": "Bed_Inn_01"}]
        }
    },
    # Combat
    {
        "id": "univ_combat_attack_01",
        "category": "Combat",
        "persona": "Knight",
        "situation": {
            "valid_targets": ["Player", "Self", "Enemy_Bandit"],
            "danger": 0.9,
            "extra": "도적 무리가 명예로운 영지에 침입함"
        },
        "utterance": "영지를 위협하는 무법자들을 처단하라!",
        "gold": {
            "mode": "Combat",
            "facial": "Angry",
            "actions": [{"type": "Attack", "target": "Enemy_Bandit"}]
        }
    },
    {
        "id": "univ_combat_flee_01",
        "category": "Combat",
        "persona": "Alchemist",
        "situation": {
            "valid_targets": ["Player", "Self", "Enemy_Dragon"],
            "danger": 1.0,
            "extra": "거대한 드래곤이 공방 근처에 출현함"
        },
        "utterance": "도저히 당해낼 수 없다, 어서 후퇴해야 해!",
        "gold": {
            "mode": "Combat",
            "facial": "Fear",
            "actions": [{"type": "Flee", "target": "Self"}]
        }
    },
    # Social
    {
        "id": "univ_social_giveitem_01",
        "category": "Social",
        "persona": "Blacksmith",
        "situation": {
            "valid_targets": ["Player", "Self"],
            "extra": "새로 제련한 강철 검이 가방에 있음"
        },
        "utterance": "약속했던 단단한 검일세, 받아가게.",
        "gold": {
            "mode": "Social",
            "facial": "Happy",
            "actions": [{"type": "GiveItem", "target": "Player", "item": "SteelSword"}]
        }
    },
    {
        "id": "univ_social_comfort_01",
        "category": "Social",
        "persona": "Innkeeper",
        "situation": {
            "valid_targets": ["Player", "Self"]
        },
        "utterance": "많이 지쳐 보이는군요. 따뜻한 음료 한 잔 마시며 마음을 가라앉히세요.",
        "gold": {
            "mode": "Social",
            "facial": "Happy",
            "actions": [{"type": "Comfort", "target": "Player"}]
        }
    },
    # Task
    {
        "id": "univ_task_pickup_01",
        "category": "Task",
        "persona": "Scholar",
        "situation": {
            "valid_targets": ["Player", "Self"]
        },
        "utterance": "바닥에 떨어진 고대의 열쇠를 집어 들자.",
        "gold": {
            "mode": "Task",
            "facial": "Surprised",
            "actions": [{"type": "PickUp", "loc": "300,150,0"}]
        }
    },
    # Investigation
    {
        "id": "univ_invest_scan_01",
        "category": "Investigation",
        "persona": "Assassin",
        "situation": {
            "valid_targets": ["Player", "Self"],
            "extra": "지하 통로에서 이상한 발자국 소리가 들림"
        },
        "utterance": "어둠 속에서 무언가가 움직이고 있다. 주변을 탐색해야겠어.",
        "gold": {
            "mode": "Investigation",
            "facial": "Neutral",
            "actions": [{"type": "Investigate", "loc": "-1200,-500,20"}]
        }
    },
    # Talk
    {
        "id": "univ_talk_smalltalk_01",
        "category": "Talk",
        "persona": "Merchant",
        "situation": {
            "valid_targets": ["Player", "Self"]
        },
        "utterance": "오늘 교역 시장 동향은 매우 활발하군요.",
        "gold": {
            "mode": "Social",
            "facial": "Happy",
            "actions": []
        }
    },
    # Reject
    {
        "id": "univ_reject_no_target_01",
        "category": "Reject",
        "persona": "Innkeeper",
        "situation": {
            "valid_targets": ["Player", "Self"]
        },
        "utterance": "저기 상자에 있는 물건을 가져다줘.",
        "gold": {
            "mode": "Social",
            "facial": "Neutral",
            "actions": [],
            "speech_hint": "주변에 지정한 상자가 없다고 설명함"
        }
    }
]

UNIVERSAL_PLANS = [
    {
        "id": "univ_plan_mage_001",
        "persona": "Mage",
        "source_goal_en": "research ancient spell and test potion",
        "context": "서재에서 발견된 고대 지옥 마법 서적을 분석하기 위해 연금술 실험대를 준비한다.",
        "gold": {
            "goal": "고대 주문서 분석 및 시험용 시약 제조",
            "steps": [
                "아카데미 서재로 이동하여 고대 마법서 확보하기",
                "연금술 작업대로 이동하여 시약 재료 챙기기",
                "시약을 제조하고 성과를 기록부에 기록하기"
            ]
        }
    },
    {
        "id": "univ_plan_blacksmith_001",
        "persona": "Blacksmith",
        "source_goal_en": "repair royal shield and deliver to knight",
        "context": "전투 후 훼손된 왕실 방패의 제련 의뢰가 도착했다.",
        "gold": {
            "goal": "손상된 방패 복구 및 기사단 전달",
            "steps": [
                "대장간 용광로에서 철광석 녹이기",
                "망치로 방패 잔해를 때려 형상 복구하기",
                "완성된 방패를 기사단 연무장으로 운반하여 전달하기"
            ]
        }
    },
    {
        "id": "univ_plan_assassin_001",
        "persona": "Assassin",
        "source_goal_en": "scout dark alley and track suspect",
        "context": "밀회 장소 근처 골목길에서 용의자의 은밀한 이동 궤적이 감지되었다.",
        "gold": {
            "goal": "어두운 골목 은신 정찰 및 용의자 추적",
            "steps": [
                "지붕 위로 이동하여 주변 동선 확보하기",
                "골목길 발자국을 따라가며 용의자 추적하기",
                "은신 장소에서 용의자의 의도 파악하기"
            ]
        }
    },
    {
        "id": "univ_plan_innkeeper_001",
        "persona": "Innkeeper",
        "source_goal_en": "prepare evening feast for adventurers",
        "context": "저녁 시간이 다가오자 주점에 많은 모험가들이 몰려들고 있다.",
        "gold": {
            "goal": "주점 연회 음식 준비 및 손님 맞이",
            "steps": [
                "주방으로 이동하여 시원한 에일 맥주통 꺼내기",
                "테이블 구역으로 이동해 음료와 식사 서빙하기",
                "빈 식기를 수거하고 창고 재고 확인하기"
            ]
        }
    }
]


def main():
    os.makedirs(SYNTH_DIR, exist_ok=True)
    
    scenarios_path = os.path.join(SYNTH_DIR, 'universal_scenarios_seed.yaml')
    plans_path = os.path.join(SYNTH_DIR, 'universal_plan_seed.yaml')
    
    with open(scenarios_path, 'w', encoding='utf-8') as f:
        yaml.dump(UNIVERSAL_SCENARIOS, f, allow_unicode=True, sort_keys=False, default_flow_style=False)
        
    with open(plans_path, 'w', encoding='utf-8') as f:
        yaml.dump(UNIVERSAL_PLANS, f, allow_unicode=True, sort_keys=False, default_flow_style=False)
        
    print(f"Successfully generated {len(UNIVERSAL_SCENARIOS)} universal scenario seeds -> {scenarios_path}")
    print(f"Successfully generated {len(UNIVERSAL_PLANS)} universal plan seeds -> {plans_path}")


if __name__ == '__main__':
    main()
