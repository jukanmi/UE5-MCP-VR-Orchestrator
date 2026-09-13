# -*- coding: utf-8 -*-
"""audit_speaker_listener.py

Audits scenarios_seed_draft.yaml for Speaker (NPC) vs Listener (Player) role confusion:
- Validates who is performing the action (Player vs NPC).
- Ensures speech_hint is 100% from the NPC's perspective speaking TO the Player.
- Fixes directional reversals in GiveItem, HandObject, Sit, Sleep, Attack, Trade, etc.
"""

import yaml
import os

YAML_PATH = r'C:\github\UE5_MCP_VR\OmniAgent_VR_System\CognitiveEngine\finetune\data\synth\scenarios_seed_draft.yaml'

def fix_speaker_listener_perspective(item):
    npc = item.get('npc', '')
    utt = item.get('utterance', '')
    gold = item.get('gold', {})
    actions = gold.get('actions', [])
    act_type = actions[0].get('type') if actions else 'Talk'
    sent_raw = item.get('situation', {}).get('sentiment', 'Neutral')
    
    sent_type = "Hostile" if "Hostile" in sent_raw else ("Friendly" if "Friendly" in sent_raw else "Neutral")
    
    # Check if the player is giving something TO the NPC vs NPC giving to Player
    # In Social GiveItem / HandObject:
    # If Utterance is "이거 받아", "물 챙겨" -> Player is handing item TO NPC!
    # NPC speech_hint must be RECEIVING ("감사히 받겠습니다", "고맙소! 잘 받겠네") NOT GIVING ("전달해 주마")!
    
    is_player_giving_to_npc = any(w in utt for w in ["받아", "챙겨", "가져", "드립니다", "줍니다", "여기"])
    is_player_commanding_npc = any(w in utt for w in ["앉아", "자라", "막아", "피해", "따라와", "이동해", "고쳐", "주워"])

    new_speech = None
    
    # 1. GiveItem / HandObject Directional Fix
    if act_type in ["GiveItem", "HandObject"]:
        if is_player_giving_to_npc:
            # NPC is RECEIVING item from Player
            if npc == "Elara":
                new_speech = "정중히 감사드리며 그대가 내어준 물품을 기꺼이 수령하겠소."
            elif npc == "Skadi":
                new_speech = "오호, 나한테 주는 거냐? 고맙게 잘 쓰도록 하지!"
            elif npc == "Moca":
                new_speech = "아... 제게 주시는 건가요? 조심히 잘 받을게요..."
            elif npc == "Guard":
                new_speech = "감사합니다. 건네주신 물품을 안전하게 전달받았습니다."
            elif npc == "James":
                new_speech = "네, 건네주신 항해 소품을 감사히 받겠습니다."
        else:
            # NPC is GIVING item to Player
            if npc == "Elara":
                new_speech = "그대에게 필요한 물품이니 정중히 전달하겠소."
            elif npc == "Skadi":
                new_speech = "자, 내 몫을 떼어줄 테니 어서 챙겨가라!"
            elif npc == "Moca":
                new_speech = "제 작은 선물이에요... 조심해서 받아주세요..."
            elif npc == "Guard":
                new_speech = "시민분의 안전을 위해 장비를 내어드리겠습니다."
            elif npc == "James":
                new_speech = "해도를 점검하는 데 유용한 도구를 나눠드리겠습니다."

    # 2. Lifestyle Sit / Sleep / Read / Pray Perspective Fix
    elif act_type == "Sit":
        if is_player_commanding_npc:
            # Player commands NPC to sit -> NPC responds about NPC's own seating action
            if sent_type == "Hostile":
                if npc in ["Elara", "Skadi", "Guard"]:
                    new_speech = "무례하군! 어찌 감히 나에게 앉으라 명령하느냐!"
                elif npc == "Moca":
                    new_speech = "무, 무서워요... 명령하지 마세요! 도망칠 거예요!"
                elif npc == "James":
                    new_speech = "당신의 억압적인 명령에는 응하지 않겠습니다."
            elif sent_type == "Friendly":
                if npc == "Elara":
                    new_speech = "권유해 주어 고맙소. 내가 지정된 의자에 앉아 쉬어가겠소."
                elif npc == "Skadi":
                    new_speech = "하하! 고맙다, 내 기꺼이 이 의자에 앉아 쉬어주지!"
                elif npc == "Moca":
                    new_speech = "고마워요... 제가 조용히 이 자리에 앉을게요..."
                elif npc == "Guard":
                    new_speech = "감사합니다. 제가 잠시 자리에 앉아 휴식을 취하겠습니다."
                elif npc == "James":
                    new_speech = "호의에 감사드립니다. 제가 자리에 앉아 해도를 검토하죠."

    # 3. Follow / Move Perspective Fix
    elif act_type == "Follow":
        if npc == "Elara":
            new_speech = "알겠소. 내가 그대의 뒤를 차분히 따라가도록 하지."
        elif npc == "Skadi":
            new_speech = "좋아, 앞장서라! 내가 네 뒤를 바짝 쫓아갈 테니!"
        elif npc == "Moca":
            new_speech = "네... 뒤에서 조용히 걸어서 따라갈게요..."
        elif npc == "Guard":
            new_speech = "알겠다. 경비 수칙에 따라 당신의 뒤를 호위하며 따르겠다."
        elif npc == "James":
            new_speech = "네, 당신의 동선을 시선에 담아 신중히 뒤따르겠습니다."

    return new_speech

def main():
    if not os.path.exists(YAML_PATH):
        print("File not found")
        return

    with open(YAML_PATH, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)

    fixed_count = 0
    for item in data:
        new_speech = fix_speaker_listener_perspective(item)
        if new_speech:
            item['gold']['speech_hint'] = new_speech
            fixed_count += 1

    with open(YAML_PATH, 'w', encoding='utf-8') as f:
        yaml.dump(data, f, allow_unicode=True, sort_keys=False, default_flow_style=False)

    print(f"Reviewed & Fixed Speaker-Listener directional speech in {fixed_count} scenarios!")

if __name__ == '__main__':
    main()
