"""발광 -> 실제 라이트 액터 — `LL_lights.json` 의 52등을 스폰한다

    UnrealEditor-Cmd.exe projectll.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_lights.py"

⚠ **`ll_place.py` 뒤에 돌린다.** `ll_place` 는 `HISM_`/`LL_Shell` 만 지우므로
  라이트는 안 건드리지만, 레벨을 새로 만들면 같이 날아간다.

## 타입 선정 근거

| 재질 | 타입 | 왜 |
|---|---|---|
| `M_TrackLED` 36 | **Spot** | 레일 스포트 — **방향성이 본질**. Point 로 두면 천장을 위로 밝혀 대비가 죽는다 |
| `M_SoffitLED` 8 | **Spot** | 매입 다운라이트 — 하우징이 물리적 컷오프를 만든다 |
| `M_PendantLED` 3 | **Point** | 링 펜던트. 사방으로 뿌리므로 원뿔이 **틀린 모형** |
| `M_CoveLED` 5 | **Rect** | 코브·카운터 **선형** 간접광. 4.2m 짜리 띠를 점광원으로 두면 폭이 죽는다 |

**Directional 은 안 쓴다** — 태양이라 실외 배경·HDRI 와 같이 가야 한다.

## 밝기·색

세기는 **루멘**으로 준다 (`ELightUnits.LUMENS`) — 칸델라·무단위와 달리
실제 기구 스펙과 직접 비교되기 때문이다.

색은 **색온도**로 준다. 블렌더 발광색을 흑체 곡선에 맞춰 재면

    M_TrackLED / M_CoveLED   (1.00, 0.82, 0.64) ≈ **3300K**
    M_PendantLED / M_SoffitLED (1.00, 0.94, 0.86) ≈ **5000K**

⚠ 이 두 무리를 그대로 쓰면 **1700K 차이라 한 방에서 색이 어긋나 보인다.**
  작가가 둔 「따뜻한 무리 / 중성 무리」 구분은 살리되 간격을 좁혀
  **3000K(레일·코브) / 4000K(펜던트·소핏)** 로 넣었다. 상수 하나씩이라
  마음에 안 들면 `SPEC` 에서 바로 바꾸면 된다.

⚠ **모빌리티는 Movable 이다.** 처음에 「붙박이니까」 Static 으로 뒀는데
  **그게 틀렸다** — Static 라이트는 **라이트맵을 구워야** 쓸 수 있고, 이
  프로젝트는 Lumen 이라 굽지 않는다. 그래서 UE 가 굽지 않은 정적 조명 위에
  `PreviewShadowsIndicator` 를 덮어 **바닥에 「Preview」 글자가 깔렸다.**
  (주방 바닥처럼 넓고 트인 면에서 제일 잘 보인다.)

  Lumen 은 Movable 라이트를 정상적으로 처리한다. 52등 정도는 문제없다.
  굳이 구워 쓸 거면 Static 으로 되돌리고 **Build Lighting 을 돌려야 한다.**

⚠ 라이트를 넣었으면 **발광은 낮춰야 한다** — 안 그러면 같은 빛을 두 번 센다.
  그건 `ll_material.py` 의 `LIGHT_CONVERTED` 가 한다 (렌즈는 여전히 빛나 보이되
  GI 기여는 거의 없는 세기로).
"""
import unreal
import json

JSON = r'C:/Project/Unreal/Import/LL_lights.json'
LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'
M = 100.0
PREFIX = 'LGT_'

# 재질별 기구 스펙 — 루멘 / 색온도 / 감쇠(cm) / 원뿔(도) / 광원 반경(cm)
#
# ⚠ **밝기 조정 이력** (전부 사용자 판단으로 내렸다)
#
#       1차  레일 800 소핏 700 펜던트 500 코브 400 = 37,900 lm (≈557 lux)  과했다
#       2차  레일 450 소핏 450 펜던트 350 코브 250 = 22,100 lm (≈325 lux)  아직 밝다
#       3차  레일 300 소핏 320 펜던트 250 코브 180 = 15,010 lm (≈221 lux)
#       4차  레일 270 소핏 290 펜던트 225 코브 160 = 13,515 lm (≈199 lux, −10%)
#       5차  레일 245 소핏 260 펜던트 200 코브 145 = 12,225 lm (≈180 lux, −9.5%)
#       6차  레일 220 소핏 235 펜던트 180 코브 130 = 10,970 lm (≈161 lux, −10.3%)
#       7차  주방 4등(420) 추가                    = 12,670 lm (≈186 lux)
#       8차  **전등 +5%** 레일 231 소핏 247 펜던트 189 코브 137 주방 441
#            = **13,303 lm (≈196 lux)**  ← 지금
#
#   총량의 **72%가 레일등 36개**라 매번 거기를 가장 많이 깎는다.
#   ⚠ lux 는 **전량이 바닥에 닿는다는 가정의 상한**이라 실제는 더 낮다.
#     밝기는 여기 네 숫자만 고치면 되고, 발광 쪽은 `ll_material.LIGHT_CONVERTED`
#     가 따로 잡는다 (두 곳을 같이 봐야 이중 계산이 안 난다).
#
# ⚠ **색온도도 살짝 올렸다** (3차) — 「너무 따뜻하다」.
#       따뜻한 무리(레일·코브)  3000 -> **3500K**
#       중성 무리(소핏·펜던트)  4000 -> **4200K**
#   두 무리 구조는 유지하되 간격을 1000 -> 700K 로 좁혀 방 전체가 중성 쪽으로
#   간다. ⚠ **라이트만 올리면 절반만 바뀐다** — 코브 띠(3.4m²)·레일 렌즈가
#   여전히 주황으로 빛나기 때문이다. 발광색은 `ll_material.EMISSIVE_TINT` 가
#   같이 민다. **두 곳을 같이 봐야 한다.**
# ⚠ **원뿔을 넓혔다** (4차) — 좁은 빔이 바닥에 동그란 웅덩이를 찍어 얼룩졌다.
#
#       레일 20°/32° -> **30°/45°**    소핏 30°/46° -> **40°/60°**
#
#   ⚠ 넓히면 **같은 루멘이 더 넓은 바닥에 퍼져 최대 조도도 같이 떨어진다.**
#     즉 이번 −10% 는 실제 체감으로는 그보다 크다 — 밝기를 더 내리기 전에
#     각도부터 보는 게 맞다.
#     레일 2.90m 높이 × 45° -> 바닥 웅덩이 지름 **2.4m** (간격 1m 라 충분히 겹친다)
#     소핏 2.80m 높이 × 60° -> 지름 **3.2m**
#
#   Point(펜던트)·Rect(코브)에는 원뿔이 없다. Rect 의 `barn_door_angle` 기본값
#   88° 는 이미 최대(90°)에 가까워 건드릴 여지가 없다.
# ⚠ **감쇠 반경은 「빛이 닿는 곳」이 아니라 「VSM 이 세는 범위」다.**
#   처음에 700~800cm 로 넉넉히 줬더니 한 점에 그림자 라이트가 **최대 31등**
#   겹쳐 UE 가 이렇게 경고했다 —
#     [VSM] One Pass Projection max lights overflow.
#   원패스 투영은 픽셀마다 「어느 로컬 라이트가 그림자를 지는가」를 비트마스크로
#   들고 있는데 그 칸이 `r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel`
#   (기본 **16**) 개뿐이다. 넘치면 느린 경로로 떨어지거나 그림자가 깨진다.
#
#   220 lm 스포트는 3.8m 에서 이미 1 lux 남짓이라 **반경을 절반으로 줄여도
#   그림이 안 바뀐다.** 0.55배로 줄이고 코브는 그림자를 끄니 31 -> 15등이 됐다.
#   (`Config/DefaultEngine.ini` 에서 한계도 24 로 올려 여유를 뒀다.)
SPEC = {
    'M_TrackLED':   {'lumens': 231, 'kelvin': 3500, 'atten': 380,
                     'inner': 30, 'outer': 45, 'src': 2.0},
    'M_SoffitLED':  {'lumens': 247, 'kelvin': 4200, 'atten': 330,
                     'inner': 40, 'outer': 60, 'src': 4.5},
    'M_PendantLED': {'lumens': 189, 'kelvin': 4200, 'atten': 440,
                     'src': 30.0},
    # 코브는 천장을 씻는 간접광이라 **그림자를 안 져도 그림이 같다** —
    # 대신 VSM 예산에서 5등이 통째로 빠진다.
    'M_CoveLED':    {'lumens': 137, 'kelvin': 3500, 'atten': 280,
                     'shadow': False},
    # 주방 매입등 4등. 홀보다 **밝고 중성적**이어야 한다 —
    #   조리공간이고, 3.2m 천장에서 20m² 를 4등으로 덮어야 한다.
    #   420 lm × 4 = 1,680 lm / 약 84 lux (홀 161 lux 는 52등 합계다).
    #   콘은 소핏(40/60)보다 벌려 바닥이 4개의 점으로 안 끊기게 한다.
    'M_KitchenLED': {'lumens': 441, 'kelvin': 4000, 'atten': 360,
                     'inner': 55, 'outer': 78, 'src': 4.5},
}

CLASS = {'spot': unreal.SpotLight, 'point': unreal.PointLight,
         'rect': unreal.RectLight}


def ue_loc(v):
    return unreal.Vector(v[0] * M, -v[1] * M, v[2] * M)


def aim_rot(aim):
    """UE 라이트는 **자기 +X** 로 쏜다. 위/아래만 쓰므로 피치로 충분하다.

    ⚠ `unreal.Rotator` 는 **(roll, pitch, yaw)** 라 위치 인자로 주면 안 된다
      (`UE_TROUBLESHOOTING.md` #46). 반드시 키워드.
    """
    if aim[2] < -0.5:
        return unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0)   # 아래로
    if aim[2] > 0.5:
        return unreal.Rotator(roll=0.0, pitch=90.0, yaw=0.0)    # 위로
    return unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0)


def main():
    with open(JSON, encoding='utf-8') as f:
        doc = json.load(f)
    lights = doc['lights']

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not (les and eas):
        unreal.log_error('에디터 서브시스템이 없다')
        return
    les.load_level(LEVEL)

    old = [a for a in eas.get_all_level_actors()
           if a.get_actor_label().startswith(PREFIX)]
    for a in old:
        eas.destroy_actor(a)
    unreal.log_warning('=== 라이트 — 기존 %d개 제거 / 새로 %d등 ==='
                       % (len(old), len(lights)))

    made = {}
    for L in lights:
        sp = SPEC.get(L['material'])
        cls = CLASS.get(L['kind'])
        if sp is None or cls is None:
            unreal.log_error('  %s: 스펙/클래스 없음 (%s / %s)'
                             % (L['name'], L['material'], L['kind']))
            continue
        a = eas.spawn_actor_from_class(cls, ue_loc(L['loc']), aim_rot(L['aim']))
        if a is None:
            unreal.log_error('  %s: 스폰 실패' % L['name'])
            continue
        a.set_actor_label(PREFIX + L['name'])
        c = a.light_component
        # ⚠ Static 으로 두면 라이트맵을 굽기 전까지 UE 가 「Preview」 표시를
        #   덮어 그린다. 이 프로젝트는 Lumen 이라 굽지 않는다.
        # ⚠ 루트 컴포넌트에만 걸면 된다. `Actor.set_mobility()` 는 없다 —
        #   그걸 불렀다가 스크립트가 중간에 죽어 레벨이 저장조차 안 됐다.
        c.set_mobility(unreal.ComponentMobility.MOVABLE)
        c.set_editor_property('intensity_units', unreal.LightUnits.LUMENS)
        c.set_editor_property('intensity', float(sp['lumens']))
        c.set_editor_property('use_temperature', True)
        c.set_editor_property('temperature', float(sp['kelvin']))
        c.set_editor_property('attenuation_radius', float(sp['atten']))
        c.set_editor_property('cast_shadows', bool(sp.get('shadow', True)))

        if L['kind'] == 'spot':
            c.set_editor_property('inner_cone_angle', float(sp['inner']))
            c.set_editor_property('outer_cone_angle', float(sp['outer']))
            c.set_editor_property('source_radius', float(sp['src']))
        elif L['kind'] == 'point':
            c.set_editor_property('source_radius', float(sp['src']))
        elif L['kind'] == 'rect':
            # aim 이 ±Z 라 렉트 평면은 XY 다. 피치 ±90 을 주면
            #   로컬 Y -> 월드 Y (source_width)
            #   로컬 Z -> 월드 X (source_height)
            # ⚠ 너무 얇으면 렉트가 선처럼 굳으므로 최소 5cm 를 준다
            sx, sy = L['size'][0] * M, L['size'][1] * M
            c.set_editor_property('source_width', max(sy, 5.0))
            c.set_editor_property('source_height', max(sx, 5.0))

        made.setdefault(L['material'], []).append(a)

    for mat in sorted(made):
        sp = SPEC[mat]
        n = len(made[mat])
        unreal.log_warning('  %-14s %-6s %2d등  %d lm × %d = %d lm  %dK  감쇠 %dcm'
                           % (mat, made[mat][0].get_class().get_name(), n,
                              sp['lumens'], n, sp['lumens'] * n,
                              sp['kelvin'], sp['atten']))
    tot = sum(SPEC[m]['lumens'] * len(v) for m, v in made.items())
    unreal.log_warning('  합계 %d등 / %d lm  (홀 바닥 68m² 기준 대략 %d lux 상한)'
                       % (sum(len(v) for v in made.values()), tot, tot / 68.0))

    # ⚠ **저장 결과를 확인한다.** `save_current_level()` 은 실패해도 예외를
    #   안 던진다 — 「저장됨」만 찍고 넘어가면 옛 값이 디스크에 남는다.
    ok = les.save_current_level()
    unreal.log_warning('  save_current_level -> %s' % ok)
    if not ok:
        unreal.log_warning('  ⚠ 실패 — save_dirty_packages 로 다시 시도한다')
        try:
            ok2 = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
            unreal.log_warning('  save_dirty_packages -> %s' % ok2)
        except Exception as e:
            unreal.log_error('  save_dirty_packages 실패: %s' % e)
    unreal.log_warning('=== 완료 — 레벨 저장됨 ===')


main()
