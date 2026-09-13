"""전체 재적용 — 임포트 → 머티리얼 → 배치 → Nanite 를 **순서대로** 한 번에

⚠ **에디터가 열려 있으면 반드시 에디터 안에서 돌릴 것.**
  `Window > Output Log` 의 콘솔을 `Python` 으로 바꾸고:

    exec(open(r'C:/Project/Unreal/Import/Scripts/ll_all.py').read())

  커맨드릿으로 돌리면 열린 에디터와 **같은 레벨을 각자 메모리에 들고** 있게
  되어 저장이 충돌한다 (`UE_TROUBLESHOOTING.md` #25).

## 순서가 전부다

    ① ll_import    FBX -> 스태틱메쉬 (슬롯이 여기서 정해진다)
    ② ll_material  마스터·MI 생성 후 **슬롯에 배정**  ← 임포트 뒤여야 한다
    ③ ll_place     셸 액터 + HISM 재생성            ← 메쉬가 있어야 한다
    ④ ll_nanite    Nanite + 그림자                  ← **배치 뒤여야 한다**

⚠ ④가 마지막인 이유 — **그림자 끄기는 컴포넌트 설정**이라 ③이 HISM 을 새로
  만들면 사라진다 (`UE_TROUBLESHOOTING.md` #23). Nanite 는 에셋 설정이라
  살아남지만, 둘을 같이 하는 스크립트라 순서를 지켜야 한다.

## 무엇만 넣을지 고르기

기본은 **셸 2개만** 다시 넣는다 (에셋 22종은 안 바뀌었을 때가 많다).
전부 다시 넣으려면 아래 `ONLY` 를 빈 문자열로 두고 실행할 것.
"""
import unreal
import os

BASE = r'C:/Project/Unreal/Import/Scripts'

# ⚠⚠ 이 사본은 **Febricula(`C:/Project/Unreal`) 의 `/Game/BurgerHouse`** 를 본다.
#    원본은 `C:/Project/UnrealProject/projectll` 의 `/Game/Burgeria` 다.
#    `Scripts\*.py` 경로를 손으로 치환해 만든 사본이니, 원본을 고치면
#    여기도 같이 갱신해야 한다.
#
# ⚠ Febricula 쪽 Props 는 2026-09-08 판(51종)이라 Burgeria(63종)와 어긋난다.
#   처음 맞출 때는 **빈 문자열로 에셋 전부** 다시 넣는다.
# 다시 넣을 FBX (쉼표 구분). 빈 문자열이면 전부.
ONLY = ''
# ⚠ **새 에셋을 추가했으면 여기에 이름을 넣고 한 번 돌릴 것.** 에셋 FBX 는
#   `Import/FBX/Assets/` 에서 읽는다 (셸만 `Import/FBX/` 바로 아래다).
#   거기에 안 넣으면 `ll_place` 가 「에셋 없음 — 건너뜀」 을 뱉는다.

# PNG 를 다시 구웠으면 '1' — 아니면 임포트를 건너뛴다
TEX_REIMPORT = ''

STEPS = ['ll_import.py', 'll_material.py', 'll_place.py', 'll_nanite.py']


def run(name):
    path = os.path.join(BASE, name)
    if not os.path.exists(path):
        unreal.log_error('  %s 가 없다' % path)
        return False
    unreal.log_warning('')
    unreal.log_warning('########## %s ##########' % name)
    try:
        with open(path, encoding='utf-8') as f:
            code = f.read()
        exec(compile(code, path, 'exec'), {'__name__': '__main__'})
        return True
    except Exception as e:
        # ⚠ 한 단계가 실패하면 **거기서 멈춘다.** 임포트가 실패했는데 배치를
        #   돌리면 옛 메쉬로 레벨을 다시 짜서 상태가 더 꼬인다.
        unreal.log_error('  %s 실패: %s' % (name, e))
        import traceback
        unreal.log_error(traceback.format_exc())
        return False


def main():
    os.environ['LL_IMPORT_ONLY'] = ONLY
    if TEX_REIMPORT:
        os.environ['LL_TEX_REIMPORT'] = TEX_REIMPORT
    else:
        os.environ.pop('LL_TEX_REIMPORT', None)

    unreal.log_warning('===== 전체 재적용 =====')
    unreal.log_warning('  다시 넣을 FBX: %s' % (ONLY or '전부'))
    unreal.log_warning('  텍스처 재임포트: %s' % (TEX_REIMPORT or '아니오'))

    for s in STEPS:
        if not run(s):
            unreal.log_error('===== %s 에서 멈췄다 — 뒤 단계는 돌리지 않는다'
                             ' =====' % s)
            return
    unreal.log_warning('')
    unreal.log_warning('===== 전부 완료 =====')
    unreal.log_warning('  ⚠ 다음으로 할 것:')
    unreal.log_warning('    1. `Build > Build Paths` — 내비를 다시 굽는다')
    unreal.log_warning('       (셸 메쉬가 바뀌면 콜리전이 바뀌므로 필수)')
    unreal.log_warning('    2. ll_nav_check.py 로 수치 검증')
    unreal.log_warning('    3. 격자 무늬가 사라졌는지 눈으로 확인')


main()
