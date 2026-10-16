// 버거리아 — StateTree 이벤트 태그
//
// LL_PROJECT.md §9.5.
//
// ## 왜 네이티브 태그인가
//
// StateTree 의 전이 트리거 `On Event` 는 **`FGameplayTag` 로 매칭**한다. 태그를
// `DefaultGameplayTags.ini` 에 손으로 넣으면 ① 사람이 에디터를 열어야 하고
// ② 오타가 나도 컴파일이 통과한다 (전이가 조용히 안 걸린다 — 함정 ②-c 와 같은
// 부류의 「조용한 실패」다).
//
// 네이티브로 박으면 태그가 **C++ 심볼**이 되어 오타가 링크 에러로 잡히고,
// 에디터 태그 피커에도 그대로 뜬다. ini 를 건드릴 일이 없다.

#pragma once

#include "NativeGameplayTags.h"

namespace BurgerHouseTags
{
	/** `BurgerHouse.Order.Ready` — 내 번호가 떴다. **Wait → Pickup 전이의 방아쇠.** */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Order_Ready);

	/** `BurgerHouse.Order.Claimed` — 트레이를 받았다 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Order_Claimed);

	/**
	 *	`BurgerHouse.Order.Lost` — 내 주문이 사라졌다 (청소에 걷혔거나 남이 가져갔다).
	 *
	 *	⚠ 이게 없으면 손님이 **Wait 에서 영원히 선다.** 주문은 좀비 청소가 걷어
	 *	  가는데 (`Sweep`), 손님은 그 사실을 알 길이 없기 때문이다.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Order_Lost);

	/**
	 *	`BurgerHouse.Queue.Advanced` — 대기열에서 내 자리가 당겨졌다.
	 *
	 *	§9.5 "⚠ **줄서기는 Smart Object 로 안 된다.** 「앞사람이 빠지면 한 칸
	 *	당겨라」는 못 준다."
	 *
	 *	⚠ 이게 왜 **이벤트**여야 하는지가 핵심이다. 대기 위치를 태스크의 출력으로
	 *	내보내도 **MoveTo 의 `Destination` 바인딩은 상태에 들어갈 때 한 번 복사된다.**
	 *	서 있는 동안 값만 바뀌면 아무도 안 움직인다. 이벤트로 Wait 를 다시 태워야
	 *	MoveTo 가 새 자리로 다시 달린다.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Queue_Advanced);
}
