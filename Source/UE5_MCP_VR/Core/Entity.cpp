#include "Core/Entity.h"

// [의도(Why)] 인터페이스는 순수 추상 계약이므로 별도 구현 로직이 없습니다.
// 각 구현체(ASmartNPC, AVRPlayerCharacter, ADroppedItemBase)에서 _Implementation 함수를 통해 오버라이드합니다.
