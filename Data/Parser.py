#!/usr/bin/env python3
"""
Febricula - 대사 스크립트 -> DT JSON 파서
 
표준 라이브러리만 사용. 언리얼 내장 파이썬(3.11)에서도 그대로 실행된다.
 
사용:
    python dialogue_parser.py Script/Day1.txt
    python dialogue_parser.py Script/*.txt --outdir Data
    python dialogue_parser.py Script/Day1.txt --enum-header Source/Febricula/Public/GameActionTypes.h
    python dialogue_parser.py Script/Day1.txt --check     # 검증만, 파일 안 씀
 
종료 코드: 0 = 성공, 1 = 에러
"""
 
import argparse
import glob
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
 
# ---------------------------------------------------------------------------
# EGameActionType - 헤더에서 뽑는 게 원칙, 못 찾으면 이 목록으로 폴백
# ---------------------------------------------------------------------------
 
FALLBACK_ACTION_TYPES = [
    "None", "AddAffinity", "GrantTag", "RemoveTag", "AddCounter",
    "CompleteEvent", "GrantTopic", "NpcSetAIState", "NpcFollowPlayer",
    "NpcStopFollow", "NpcStartPatrol", "NpcStartGuide", "NpcStopRouteMove",
    "NpcResumeAI", "NpcPauseAI", "NpcPlayAnim", "NpcStopAnim",
    "NpcFacePlayer", "TimeAdvanceDay", "TimeAdvanceSchedule", "ChangeLevel",
]
 
FALLBACK_CONDITION_TYPES = [
    "None", "DayAtLeast", "EventCompleted", "EventNotCompleted",
    "RelationshipAtLeast", "HasTag", "NotHasTag", "CounterAtLeast",
    "CounterAtMost", "StatAtLeast", "RollCheck", "DayEquals", "DayAtMost",
    "MonthEquals", "ScheduleIs", "ScheduleIsChildOf", "DayTypeIs",
    "DayTypeIsChildOf", "IsWeekend", "IsWeekday",
]
 
# AutoAdvance 계열은 액션이 아니라 노드 필드라 별도 처리
AUTO_COMMANDS = {"wait", "wait_near", "wait_arrive", "hide"}
FLOW_COMMANDS = {"cast", "label", "menu", "jump", "end", "branch", "if", "cam", "SE3d"}
 
EMPTY = "None"   # UE에서 빈 FName의 표현
 
 
def extract_enum(header_path, enum_name):
    """C++ 헤더에서 지정한 enum의 enumerator 이름을 뽑는다.
 
    목록을 파서에 하드코딩하면 enum이 늘어날 때마다 두 곳을 고쳐야 하므로
    소스를 직접 읽는다. 실패하면 폴백 목록을 쓴다.
    """
    try:
        text = Path(header_path).read_text(encoding="utf-8-sig")
    except OSError:
        return None
 
    match = re.search(
        r"enum\s+class\s+" + re.escape(enum_name) + r"\s*(?::\s*\w+\s*)?\{(.*?)\}\s*;",
        text,
        re.DOTALL,
    )
    if not match:
        return None
 
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", "", body)
 
    names = []
    for line in body.splitlines():
        m = re.match(r"\s*([A-Za-z_]\w*)\s*(?:=|,|UMETA|$)", line)
        if m:
            names.append(m.group(1))
    return names or None
 
 
# ---------------------------------------------------------------------------
# 데이터 모델
# ---------------------------------------------------------------------------
 
@dataclass
class Action:
    type: str
    key: str = EMPTY
    key2: str = EMPTY
    value: int = 0
 
    def to_json(self):
        return {"Type": self.type, "Key": self.key, "Key2": self.key2, "Value": self.value}
 
 
@dataclass
class Condition:
    """FGameCondition - Key2가 없어서 인자 규칙이 액션보다 단순하다."""
    type: str
    key: str = EMPTY
    value: int = 0
 
    def to_json(self):
        return {"Type": self.type, "Key": self.key, "Value": self.value}
 
 
@dataclass
class Branch:
    conditions: list = field(default_factory=list)
    target: str = EMPTY
    line: int = 0
 
    def to_json(self):
        return {
            "Conditions": [c.to_json() for c in self.conditions],
            "NextNodeId": self.target,
        }
 
 
@dataclass
class Choice:
    text: str
    target: str = ""            # "" = 메뉴 블록 다음 노드로 합류
    actions: list = field(default_factory=list)
    conditions: list = field(default_factory=list)
    yes_no: bool = False
    line: int = 0
 
    def to_json(self):
        return {
            "Text": self.text,
            "Conditions": [c.to_json() for c in self.conditions],
            "Actions": [a.to_json() for a in self.actions],
            "YesNo": self.yes_no,
            "NextNodeId": self.target or EMPTY,
        }
 
 
@dataclass
class Node:
    name: str = ""
    speaker_name: str = ""
    speaker_npc_id: str = EMPTY
    text: str = ""
    actions: list = field(default_factory=list)
    choices: list = field(default_factory=list)
    choice_yes_or_no: bool = False
    branches: list = field(default_factory=list)
    next_id: str = ""           # "" = 미정, 2패스에서 채움
    next_fixed: bool = False    # jump/end/menu로 확정됨
    auto: str = "None"
    auto_value: float = 0.0
    auto_npc: str = EMPTY
    hide_box: bool = False
    cam_mode: str = "None"
    cam_loc: tuple = (0.0, 0.0, 0.0)
    cam_rot: tuple = (0.0, 0.0, 0.0)     # 디테일 패널 순서: X=Roll, Y=Pitch, Z=Yaw
    cam_blend: float = 0.0
    cam_focus: float = 100000.00

    se_channel = ""
    se_name = ""
    se_target = ""
    se_blend = 0.0

    phantom: bool = False        # 붙을 대사가 없어 파서가 만들어낸 노드
    line: int = 0
 
    def to_json(self):
        data = {
            "Name": self.name,
            "SpeakerName": self.speaker_name,
            "SpeakerNpcId": self.speaker_npc_id,
            "Text": self.text,
            "OnEnterActions": [a.to_json() for a in self.actions],
            "Choices": [c.to_json() for c in self.choices],
            "bYesNo": self.choice_yes_or_no,
            "Branches": [b.to_json() for b in self.branches],
            "NextNodeId": self.next_id or EMPTY,
            "AutoAdvance": self.auto,
            "AutoAdvanceValue": self.auto_value,
            "AutoAdvanceNpcId": self.auto_npc,
            "bHideBoxWhileWaiting": self.hide_box,
            # FGameCamera - 항상 내보낸다. 임포트가 필드를 못 찾아 무시하는 것보다
            # 기본값이라도 명시적으로 들어가 있는 편이 안전하다.
            "Camera": {
                "CameraType": self.cam_mode,
                "Location": {
                    "X": self.cam_loc[0], "Y": self.cam_loc[1], "Z": self.cam_loc[2],
                },
                "Rotation": {
                    "Roll": self.cam_rot[0], "Pitch": self.cam_rot[1], "Yaw": self.cam_rot[2],
                },
                "MoveTime": self.cam_blend,
                "Focus": self.cam_focus,
            },
            "SoundLocation": {
                "SoundChannel": self.se_channel,
                "SoundName": self.se_name,
                "Target": self.se_target,
                "Blend": self.se_blend,
            }
        }
        return data
 
 
# ---------------------------------------------------------------------------
# 파서
# ---------------------------------------------------------------------------
 
class Parser:
    def __init__(self, filename, action_types, condition_types):
        self.filename = filename
        self.action_types_lower = {t.lower(): t for t in action_types}
        self.condition_types_lower = {t.lower(): t for t in condition_types}
        self.p_conditions = []      # @branch가 소비할 때까지 쌓이는 조건
 
        self.cast = {}          # 별칭 -> (SpeakerName, SpeakerNpcId)
        self.nodes = []
        self.errors = []
        self.warnings = []
 
        # 다음 노드에 붙을 대기 상태
        self.p_label = None
        self.p_label_line = 0
        self.p_actions = []
        self.p_auto = None      # (mode, value, npc)
        self.p_hide = False
        self.p_cam = None       # (mode, loc, rot, blend)
        self.p_se = None        # (channel, name, target, blend)
        self.p_line = 0
 
        self.auto_counter = 0
        self.last_label = "N"
        self.terminator = None   # @jump/@end 직후인지 (그 뒤 내용은 도달 불가)
 
    # -- 리포트 -------------------------------------------------------------
 
    def error(self, line, message):
        self.errors.append((line, message))
 
    def warn(self, line, message):
        self.warnings.append((line, message))
 
    # -- 화자 ---------------------------------------------------------------
 
    def resolve_cast(self, alias, line):
        if alias in self.cast:
            return self.cast[alias]
        self.error(line, f"선언되지 않은 화자 '{alias}'")
        return ("", EMPTY)
 
    def resolve_npc(self, token):
        """액션 인자에 별칭이 오면 NpcId로 치환. 아니면 그대로."""
        if token in self.cast:
            npc = self.cast[token][1]
            if npc and npc != EMPTY:
                return npc
        return token
 
    # -- 노드 생성 ----------------------------------------------------------

    def ensure_routing_node(self, line_no):
        """@label 직후 대사 없이 @branch/@jump가 오면 분기 전용 노드를 만든다."""
        if self.p_label is None:
            return
        self.commit(Node(line=line_no))
 
    def next_name(self):
        if self.p_label:
            name = self.p_label
            self.last_label = name
            self.p_label = None
            self.auto_counter = 0
            return name
        self.auto_counter += 1
        return f"{self.last_label}_{self.auto_counter:03d}"
 
    def commit(self, node):
        """대기 중인 액션/AutoAdvance를 노드에 붙이고 목록에 넣는다."""
        if self.terminator:
            t_line, t_cmd = self.terminator
            self.error(node.line,
                       f"@{t_cmd}({t_line}행) 뒤의 내용은 도달할 수 없음 - "
                       f"@label로 새 구간을 시작하거나, 이 내용을 @{t_cmd} 앞으로 옮길 것")
            self.terminator = None
 
        if self.p_conditions:
            self.error(node.line, "@if 조건이 @branch에 연결되지 않음")
            self.p_conditions = []
 
        node.name = self.next_name()
        node.actions = self.p_actions
        self.p_actions = []
 
        if self.p_auto:
            mode, value, npc = self.p_auto
            node.auto = mode
            node.auto_value = value
            node.auto_npc = npc
            self.p_auto = None
 
        node.hide_box = self.p_hide
        self.p_hide = False
        self.p_line = 0
 
        if self.p_cam:
            if len(self.p_cam) < 5:
                node.cam_mode, node.cam_loc, node.cam_rot, node.cam_blend = self.p_cam
            else:
                node.cam_mode, node.cam_loc, node.cam_rot, node.cam_blend, node.cam_focus = self.p_cam
            self.p_cam = None

        if self.p_se:
            if len(self.p_se) == 2:
                node.se_channel, node.se_name = self.p_se
            elif len(self.p_se) == 3:
                node.se_channel, node.se_name, node.se_target = self.p_se
            elif len(self.p_se) == 4:
                node.se_channel, node.se_name, node.se_target, node.se_blend = self.p_se

            self.p_se = None

        if node.hide_box and node.auto == "None":
            self.error(node.line,
                       "@hide를 썼으나 @wait 계열이 없음 - 대화창이 닫힌 채 영구 정지한다")

        self.nodes.append(node)
        return node
 
    def mark_pending(self, line):
        if not self.has_pending():
            self.p_line = line
 
    def has_pending(self):
        return (bool(self.p_actions) or self.p_auto is not None
                or self.p_hide or self.p_cam or self.p_se is not None)
 
    def close_pending(self, line):
        """대사 없이 남은 대기 상태를 빈 노드로 닫는다 (이동 대기 노드)."""
        if not self.has_pending():
            if self.p_label:
                self.error(self.p_label_line,
                           f"라벨 '{self.p_label}'에 붙을 노드가 없음")
                self.p_label = None
            return
        if not self.p_hide and self.p_auto is None:
            self.error(self.p_line or line,
                       "이 명령이 붙을 대사가 없음 - @cam과 액션은 바로 다음 대사 노드에 붙는다. "
                       "적용할 대사 위로 옮기거나 대사를 한 줄 붙일 것")
            phantom = False
        else:
            phantom = True
        temp_node = Node(line=self.p_line or line)

        #if len(temp_node.choices) > 0:


        node = self.commit(Node(line=self.p_line or line))
        node.phantom = phantom
 
    # -- 인자 배정 ----------------------------------------------------------
 
    def build_action(self, type_name, args, line):
        canonical = self.action_types_lower.get(type_name.lower())
        if canonical is None:
            self.error(line, f"알 수 없는 액션 '{type_name}'")
            return None
 
        key, key2, value = EMPTY, EMPTY, 0
        slot = 0
        for arg in args:
            try:
                value = int(arg)
                continue    
            except ValueError:
                pass
            if slot == 0:
                key = self.resolve_npc(arg)
                slot = 1
            elif slot == 1:
                key2 = self.resolve_npc(arg)
                slot = 2
            else:
                self.error(line, f"'{canonical}'에 인자가 너무 많음: '{arg}'")
        return Action(canonical, key, key2, value)
 
    def build_condition(self, type_name, args, line):
        canonical = self.condition_types_lower.get(type_name.lower())
        if canonical is None:
            self.error(line, f"알 수 없는 조건 '{type_name}'")
            return None
 
        key, value, has_key = EMPTY, 0, False
        for arg in args:
            try:
                value = int(arg)
                continue
            except ValueError:
                pass
            if not has_key:
                key = self.resolve_npc(arg)
                has_key = True
            else:
                self.error(line, f"'{canonical}'에 인자가 너무 많음: '{arg}'")
        return Condition(canonical, key, value)
 
    # -- 메인 루프 ----------------------------------------------------------
 
    def parse(self, text):
        raw = text.splitlines()
        i = 0
        in_menu = False
        menu_node = None
 
        while i < len(raw):
            line_no = i + 1
            line = raw[i].strip()
            i += 1
 
            if not line or line.startswith("//"):
                continue
 
            # ---- 메뉴 블록 ----
            if in_menu:
                if line.startswith("-"):
                    body = line[1:].strip()
                    target = ""
                    if "->" in body:
                        body, target = (p.strip() for p in body.split("->", 1))
                    if not body:
                        self.error(line_no, "선택지 텍스트가 비어있음")
                    menu_node.choices.append(Choice(body, target, line=line_no))
                    continue
                if line.startswith("@"):
                    head = line[1:].split()
                    cmd = head[0] if head else ""
                    if cmd.lower() == "if" and menu_node.choices:
                        if not head[1:]:
                            self.error(line_no, "@if에 조건이 없음")
                        else:
                            cond = self.build_condition(head[1], head[2:], line_no)
                            if cond:
                                menu_node.choices[-1].conditions.append(cond)
                        continue
                    if cmd.lower() in self.action_types_lower and menu_node.choices:
                        act = self.build_action(cmd, head[1:], line_no)
                        if act:
                            menu_node.choices[-1].actions.append(act)
                        continue
                # 메뉴 종료 - 이 줄은 다시 처리
                in_menu = False
                if not menu_node.choices:
                    self.error(menu_node.line, "@menu인데 선택지가 하나도 없음")
                i -= 1
                continue
 
            # ---- 명령 ----
            if line.startswith("@"):
                parts = line[1:].split()
                if not parts:
                    self.error(line_no, "빈 명령")
                    continue
                cmd, args = parts[0], parts[1:]
                low = cmd.lower()
 
                if low == "cast":
                    self.cmd_cast(line, line_no)
 
                elif low == "label":
                    if not args:
                        self.error(line_no, "@label에 이름이 없음")
                        continue
                    self.close_pending(line_no)
                    self.p_label = args[0]
                    self.p_label_line = line_no
                    self.terminator = None
 
                elif low == "menu":
                    if not self.nodes:
                        self.error(line_no, "@menu 앞에 대사 노드가 없음")
                        continue
                    menu_node = self.nodes[-1]
                    menu_node.next_fixed = True
                    menu_node.next_id = EMPTY

                    menu_node.yes_no = False

                    if len(parts) == 2:
                        if parts[1] == "YesNo":
                            menu_node.choice_yes_or_no = True


                    if menu_node.auto != "None":
                        self.warn(line_no, "선택지 노드의 AutoAdvance는 무시되므로 None으로 강제함")
                        menu_node.auto = "None"
                        menu_node.auto_value = 0.0
                        menu_node.auto_npc = EMPTY
                    in_menu = True
 
                elif low == "jump":
                    self.close_pending(line_no)
                    if not args:
                        self.error(line_no, "@jump에 대상이 없음")
                    elif not self.nodes:
                        self.error(line_no, "@jump 앞에 노드가 없음")
                    else:
                        self.nodes[-1].next_id = args[0]
                        self.nodes[-1].next_fixed = True
                        self.terminator = (line_no, "jump")
 
                elif low == "end":
                    self.close_pending(line_no)
                    if not self.nodes:
                        self.error(line_no, "@end 앞에 노드가 없음")
                    else:
                        self.nodes[-1].next_id = EMPTY
                        self.nodes[-1].next_fixed = True
                        self.terminator = (line_no, "end")
 
                elif low == "if":
                    if not args:
                        self.error(line_no, "@if에 조건이 없음")
                        continue
                    cond = self.build_condition(args[0], args[1:], line_no)
                    if cond:
                        self.p_conditions.append(cond)
 
                elif low == "branch":
                    if not args:
                        self.error(line_no, "@branch에 대상이 없음")
                    elif not self.p_conditions:
                        self.error(line_no, "@branch 앞에 @if 조건이 없음 - 무조건 분기라면 @jump를 쓸 것")
                    else:
                        # commit()이 남은 조건을 에러로 보므로 먼저 빼둔다
                        conds, self.p_conditions = self.p_conditions, []
                        self.ensure_routing_node(line_no)
                        if not self.nodes:
                            self.error(line_no, "@branch 앞에 노드가 없음")
                        else:
                            self.nodes[-1].branches.append(Branch(conds, args[0], line_no))

                elif low == "cam":
                    self.mark_pending(line_no)
                    self.cmd_cam(args, line_no)

                elif low == "playse3d":
                    self.mark_pending(line_no)
                    self.cmd_se(args, line_no)

                elif low in AUTO_COMMANDS:
                    self.mark_pending(line_no)
                    self.cmd_auto(low, args, line_no)
 
                elif low in FLOW_COMMANDS:
                    self.warn(line_no, f"'@{cmd}'는 아직 구현되지 않음 - 무시")
 
                else:
                    act = self.build_action(cmd, args, line_no)
                    if act:
                        self.mark_pending(line_no)
                        self.p_actions.append(act)
                continue
 
            # ---- 이어붙이기 ----
            if line.startswith("|"):
                if not self.nodes:
                    self.error(line_no, "'|' 앞에 대사가 없음")
                else:
                    self.nodes[-1].text += "\n" + line[1:].strip()
                continue
 
            # ---- 대사 ----
            self.cmd_dialogue(line, line_no)
 
        if in_menu and not menu_node.choices:
            self.error(menu_node.line, "@menu인데 선택지가 하나도 없음")
        self.close_pending(len(raw))
        self.link()
        return self.nodes
 
    # -- 개별 명령 ----------------------------------------------------------
 
    def cmd_cast(self, line, line_no):
        m = re.match(r'@cast\s+(\S+)\s*=\s*"([^"]*)"\s*(\S+)?', line, re.IGNORECASE)
        if not m:
            self.error(line_no, '@cast 형식 오류 - @cast 별칭 = "표시이름" NpcId')
            return
        alias, display, npc = m.group(1), m.group(2), m.group(3) or EMPTY
        if alias in self.cast:
            self.warn(line_no, f"화자 '{alias}'가 이미 선언됨 - 덮어씀")
        self.cast[alias] = (display, npc.strip('"') if npc else EMPTY)
 
    def cmd_auto(self, cmd, args, line_no):
        if cmd == "hide":
            self.p_hide = True
            return
 
        if self.p_auto is not None:
            self.warn(line_no, "@wait 계열이 연속으로 지정됨 - 마지막 것만 적용")
 
        if cmd == "wait":
            if not args:
                self.error(line_no, "@wait에 초가 없음")
                return
            try:
                seconds = float(args[0])
            except ValueError:
                self.error(line_no, f"@wait의 값이 숫자가 아님: '{args[0]}'")
                return
            if seconds <= 0:
                self.warn(line_no, f"@wait {seconds} - 즉시 넘어간다")
            self.p_auto = ("AfterDelay", seconds, EMPTY)
            return
 
        # wait_near / wait_arrive
        mode = "WhenPlayerNearNpc" if cmd == "wait_near" else "WhenNpcArrived"
        npc, dist = EMPTY, None
        for arg in args:
            try:
                dist = float(arg)
            except ValueError:
                npc = self.resolve_npc(arg)
        if dist is None:
            self.error(line_no, f"@{cmd}에 거리(cm)가 없음")
            return
        if dist <= 0:
            self.error(line_no, f"@{cmd} 거리가 {dist}cm - 판정이 성립하지 않는다")
        elif dist < 50:
            self.warn(line_no, f"@{cmd} 거리가 {dist}cm - 초 단위를 넣은 게 아닌지 확인")
        self.p_auto = (mode, dist, npc)
 
    def cmd_cam(self, args, line_no):
        if self.p_cam is not None:
            self.warn(line_no, "@cam이 연속으로 지정됨 - 마지막 것만 적용")
 
        if args and args[0].lower() in ("player", "reset"):
            blend = 0.0
            if len(args) > 1:
                try:
                    blend = float(args[1])
                except ValueError:
                    self.error(line_no, f"@cam reset의 전환 시간이 숫자가 아님: '{args[1]}'")
                    return
            self.p_cam = ("Player", (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), blend)
            return
 
        # @cam cinematic x y z  p y r  [blend]   또는
        # @cam cinematic Loc x y z Rot p y r [blend]  (Loc/Rot 키워드는 선택)
        if args and args[0].lower() in ("cinematic", "cine"):
            args = args[1:]
 
        if not args:
            self.error(line_no, "@cam에 값이 없음 "
                                "(예: @cam cinematic 1240 -330 180 0 -12 0 0.8)")
            return
 
        # 키워드 없이 숫자만 온 경우: 앞 3개 위치, 다음 3개 회전, 나머지 하나 전환 시간
        if args[0].lower() not in ("loc", "rot"):
            numbers = []
            for token in args:
                try:
                    numbers.append(float(token))
                except ValueError:
                    self.error(line_no, f"@cam의 값이 숫자가 아님: '{token}'")
                    return
            if len(numbers) not in (6, 7, 8):
                self.error(line_no,
                           f"@cam에 값이 6개(위치3+회전3) 또는 7개(+전환시간) 필요함 "
                           f"(받은 값: {len(numbers)}개)")
                return
            loc = tuple(numbers[0:3])
            rot = tuple(numbers[3:6])
            blend = numbers[6] if len(numbers) == 7 else 0.0
            focus = numbers[7] if len(numbers) == 8 else 100000.0
            if blend < 0:
                self.error(line_no, f"@cam 전환 시간이 음수: {blend}")
                return
            if focus < 0:
                self.error(line_no, f"@cam 포커스 값이 음수: {blend}")
            self.p_cam = ("Cinematic", loc, rot, blend, focus)
            return
 
        loc, rot, blend, focus, seen = None, None, 0.0, 10000.0, None
        buffer = []
 
        def flush():
            nonlocal loc, rot
            if seen is None:
                return True
            if len(buffer) != 3:
                self.error(line_no, f"@cam {seen}에 값이 3개 필요함 (받은 값: {len(buffer)}개)")
                return False
            if seen == "Loc":
                loc = tuple(buffer)
            else:
                rot = tuple(buffer)
            return True
        check_blend = False
        for i, token in enumerate(args):
            low = token.lower()
            if low in ("loc", "rot"):
                if not flush():
                    return
                seen = "Loc" if low == "loc" else "Rot"
                buffer = []
                continue
            try:
                number = float(token)
            except ValueError:
                self.error(line_no, f"@cam의 값이 숫자가 아님: '{token}'")
                return
            if seen is None:
                self.error(line_no, "@cam은 Loc 또는 Rot로 시작해야 함")
                return
            if len(buffer) < 3:
                buffer.append(number)
            else:
                if i == 8:
                    blend = number
                    check_blend = True
                elif i == 9:
                    focus = number
        if not flush():
            return

        if loc is None or rot is None:
            self.error(line_no, "@cam에 Loc과 Rot이 모두 필요함 "
                                "(예: @cam Loc 1240 -330 180 Rot 0 -12 0 0.8)")
            return
        if blend < 0:
            self.error(line_no, f"@cam 전환 시간이 음수: {blend}")
            return
        self.p_cam = ("Cinematic", loc, rot, blend, focus)

    def cmd_se(self, args, line_no):
        if not args:
            self.error(line_no, "@PlaySE3d에 값이 없음")
            return
        self.p_se = (args[0], args[1], None, 0)
        if len(args) == 3:
            self.p_se = (args[0], args[1], args[2], 0)
        elif len(args) == 4:
            self.p_se = (args[0], args[1], args[2], args[3])

        return

    def cmd_dialogue(self, line, line_no):
        node = Node(line=line_no)
 
        if line.startswith(":"):
            # 같은 화자 계속
            if not self.nodes:
                self.error(line_no, "':' 앞에 화자가 지정된 대사가 없음")
                return
            prev = self.nodes[-1]
            node.speaker_name = prev.speaker_name
            node.speaker_npc_id = prev.speaker_npc_id
            node.text = line[1:].strip()
        elif ":" in line:
            alias, body = line.split(":", 1)
            alias = alias.strip()
            display, npc = self.resolve_cast(alias, line_no)
            node.speaker_name = display
            node.speaker_npc_id = npc
            node.text = body.strip()
        else:
            node.text = line   # 나레이션
 
        self.commit(node)
 
    # -- 2패스 -------------------------------------------------------------
 
    def link(self):
        names = {}
        for node in self.nodes:
            if node.name in names:
                self.error(node.line,
                           f"라벨 '{node.name}'이 중복됨 (앞서 {names[node.name]}행)")
            names[node.name] = node.line
 
        for index, node in enumerate(self.nodes):
            if not node.next_fixed:
                node.next_id = self.nodes[index + 1].name if index + 1 < len(self.nodes) else EMPTY
            # 메뉴 선택지 중 목적지 생략된 것 -> 블록 다음 노드로 합류
            for choice in node.choices:
                if not choice.target:
                    choice.target = (self.nodes[index + 1].name
                                     if index + 1 < len(self.nodes) else EMPTY)
 
        # 점프 전용 라벨로 흘러 들어가는 것을 막는다.
        # 선택지/분기 대상은 명시적으로 진입해야 하는 지점이므로,
        # 자동 연결로 흘러 들어가면 다른 가지의 대사가 이어서 재생된다.
        jump_targets = set()
        for node in self.nodes:
            jump_targets.update(c.target for c in node.choices)
            jump_targets.update(b.target for b in node.branches)
 
        for index, node in enumerate(self.nodes):
            if node.next_fixed or node.phantom or index + 1 >= len(self.nodes):
                continue
            following = self.nodes[index + 1].name
            if following in jump_targets:
                self.error(
                    node.line,
                    f"'{node.name}'이(가) '{following}'으로 흘러 들어감 - "
                    f"'{following}'은 선택지/분기 대상이므로 @jump나 @end로 끝맺어야 한다")
 
        # 레벨 전환은 대화의 마지막에서만 일어나야 한다.
        # 중간에 있으면 전환 후에도 대화가 이어져, 새 레벨에 없는 액터를 찾게 된다.
        for node in self.nodes:
            if not any(a.type == "ChangeLevel" for a in node.actions):
                continue
            if node.next_id and node.next_id != EMPTY:
                self.error(node.line,
                           f"'{node.name}'에 @ChangeLevel이 있으나 대화가 이어짐 "
                           f"(다음: {node.next_id}) - 레벨 전환 노드는 @end로 끝내야 한다")
            if node.choices or node.branches:
                self.error(node.line,
                           f"'{node.name}'에 @ChangeLevel과 선택지/분기가 함께 있음")
 
        # 참조 검사
        for node in self.nodes:
            if node.next_id and node.next_id != EMPTY and node.next_id not in names:
                self.error(node.line, f"이동 대상 '{node.next_id}'를 찾을 수 없음")
            for choice in node.choices:
                if choice.target != EMPTY and choice.target not in names:
                    self.error(choice.line, f"선택지 대상 '{choice.target}'를 찾을 수 없음")
            for branch in node.branches:
                if branch.target not in names:
                    self.error(branch.line, f"분기 대상 '{branch.target}'를 찾을 수 없음")
 
        # 도달 가능성 (첫 노드에서 출발)
        if self.nodes:
            reached, stack = set(), [self.nodes[0].name]
            by_name = {n.name: n for n in self.nodes}
            while stack:
                cur = stack.pop()
                if cur in reached or cur not in by_name:
                    continue
                reached.add(cur)
                node = by_name[cur]
                if node.next_id and node.next_id != EMPTY:
                    stack.append(node.next_id)
                stack.extend(c.target for c in node.choices if c.target != EMPTY)
                stack.extend(b.target for b in node.branches if b.target != EMPTY)
            for node in self.nodes:
                if node.name not in reached and not node.phantom:
                    self.warn(node.line, f"'{node.name}'에 도달할 수 없음")
 
 
# ---------------------------------------------------------------------------
# 엔트리포인트
# ---------------------------------------------------------------------------
 
def process(path, action_types, condition_types, outdir, check_only):
    parser = Parser(Path(path).name, action_types, condition_types)
    nodes = parser.parse(Path(path).read_text(encoding="utf-8-sig"))
 
    for line_no, message in sorted(parser.errors):
        print(f"{parser.filename}({line_no}): [ERROR] {message}")
    #for line_no, message in sorted(parser.warnings):
    #    print(f"{parser.filename}({line_no}): [WARN ] {message}")
 
    if parser.errors:
        print(f"{parser.filename}: 실패 - 에러 {len(parser.errors)}건")
        return False
 
    if not check_only:
        target = Path(outdir or Path(path).parent) / (Path(path).stem + ".json")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(
            json.dumps([n.to_json() for n in nodes], ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        print(f"{parser.filename}: 노드 {len(nodes)}개 -> {target}")
    else:
        print(f"{parser.filename}: 노드 {len(nodes)}개, 이상 없음")
    return True
 
 
def main():
    ap = argparse.ArgumentParser(description="Febricula dialogue script parser")
    ap.add_argument("inputs", nargs="*", default=["Mainstory1.txt", "DT_DoorDialogue.txt", "FixedDialogue.txt", "DT_SuahDialogue.txt", "DT_DefaultDialogue.txt", "DT_DoyunDialogue.txt", "DT_MobeDialogue.txt"], help="스크립트 파일 (와일드카드 가능)")
    ap.add_argument("--outdir", help="JSON 출력 폴더 (기본: 입력 파일과 같은 위치)")
    ap.add_argument("--enum-header", nargs="*", default=["D:\\febricula\\Febricula\\Source\\Febricula\\Public\\Dialogue\\DialogueDataTypes.h"],
                    help="EGameActionType / EGameConditionType이 정의된 .h 경로 (여러 개 가능)")
    ap.add_argument("--check", action="store_true", help="검증만 하고 파일을 쓰지 않음")
    args = ap.parse_args()

    def load(enum_name, fallback, label):
        for header in args.enum_header:
            found = extract_enum(header, enum_name)
            if found:
                print(f"{label} {len(found)}개를 {Path(header).name}에서 읽음")
                return found
        if args.enum_header:
            print(f"경고: 헤더에서 {enum_name}을 찾지 못함 - 내장 목록 사용", file=sys.stderr)
        return fallback
 
    types = load("EGameActionType", FALLBACK_ACTION_TYPES, "액션 타입")
    conditions = load("EGameConditionType", FALLBACK_CONDITION_TYPES, "조건 타입")
 
    paths = []
    for pattern in args.inputs:
        found = glob.glob(pattern)
        paths.extend(found if found else [pattern])
 
    ok = True
    for path in paths:
        if not process(path, types, conditions, args.outdir, args.check):
            ok = False
    return 0 if ok else 1
 
 
if __name__ == "__main__":
    sys.exit(main())
