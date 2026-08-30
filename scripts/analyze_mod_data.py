#!/usr/bin/env python3
"""Audit converted MOD magic, goods, NPC, and object data against current C++ loaders."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SECTION_RE = re.compile(r"^\s*\[([^\]]+)\]")
KEY_VALUE_RE = re.compile(r"^\s*([^#;\[\]=][^=]*?)\s*=\s*(.*)$")
LEVEL_SECTION_RE = re.compile(r"^level\d+$", re.IGNORECASE)
RUNTIME_GENERATED_DIRS = {"save"}

MAGIC_GENERAL_KEYS = {
    "name",
    "type",
    "injurytype",
    "spritetype",
    "attribute",
    "scriptfile",
    "intro",
    "image",
    "icon",
    "flyingimage",
    "flyingsound",
    "vanishimage",
    "vanishsound",
    "leapimage",
    "actionfile",
    "actionshadowfile",
    "useactionfile",
    "attackfile",
    "flymagic",
    "explodemagicfile",
    "randmagicfile",
    "randmagicprobability",
    "secondmagicfile",
    "secondmagicdelay",
    "magicwhennewpos",
    "magictousewhenkillenemy",
    "magicdirectionwhenkillenemy",
    "bouncefly",
    "bounceflyspeed",
    "bounceflyendhurt",
    "bounceflytouchhurt",
    "bounceflyendmagic",
    "magicdirectionwhenbounceflyend",
    "carryuser",
    "carryuserspriteindex",
    "hideuserwhencarry",
    "ball",
    "sticky",
    "solid",
    "discardoppositemagic",
    "exchangeuser",
    "changemagic",
    "hitcounttochangemagic",
    "hitcountflyradius",
    "hitcountflyanglespeed",
    "hitcountflyingimage",
    "hitcountvanishimage",
    "jumptotarget",
    "jumpmovespeed",
    "jumpendmagic",
    "replacemagic",
    "specialkind9replaceflyini",
    "specialkind9replaceflyini2",
    "flyini",
    "flyini2",
    "magictousewhenbeattacked",
    "magicdirectionwhenbeattacked",
    "flyinterval",
    "supermodeimage",
    "supermodesound",
    "regionfile",
    "keepmilliseconds",
    "maxlevel",
    "goodsname",
    "npcfile",
    "maxcount",
    "bodyradius",
    "disableuse",
    "lifefulltouse",
    "vibratingscreen",
    "additionaleffect",
    "belong",
    "bounce",
    "bouncehurt",
    "nospecialkindeffect",
    "nospecialkindeffectext",
    "sideeffecttype",
    "sideeffectpercent",
    "sideeffectprobability",
    "nointerruption",
    "disablemovemilliseconds",
    "disableskillmilliseconds",
    "coldmilliseconds",
    "dieafteruse",
    "restoretype",
    "restorepercent",
    "restoreprobability",
    "attackaddpercent",
    "defendaddpercent",
    "evadeaddpercent",
    "speedaddpercent",
    "morphmilliseconds",
    "weakmilliseconds",
    "weakattackpercent",
    "weakdefendpercent",
    "blindmilliseconds",
    "parasitic",
    "parasiticmagic",
    "parasiticinterval",
    "parasiticmaxeffect",
    "changetofriendmilliseconds",
    "passthrough",
    "attackall",
    "traceenemy",
    "tracespeed",
    "traceenemydelaymilliseconds",
    "followmouse",
    "moveimitateuser",
    "moveback",
    "randommovedegree",
    "meteormove",
    "meteormovedir",
    "circlemovecolockwise",
    "circlemoveclockwise",
    "circlemoveanticlockwise",
    "roundmovecolockwise",
    "roundmoveclockwise",
    "roundmoveanticlockwise",
    "roundmovecount",
    "roundmovedegreespeed",
    "roundradius",
    "beginatmouse",
    "beginatuser",
    "beginatuseradddirectionoffset",
    "beginatuseradduserdirectionoffset",
    "noexplodewhenlifeframeend",
    "explodewhenlifeframeend",
    "passthroughwithdestroyeffect",
    "passthroughwall",
    "revivebodyradius",
    "revivebodymaxcount",
    "revivebodylifemilliseconds",
    "rangeeffect",
    "rangeradius",
    "rangespeedup",
    "rangetimeinerval",
    "rangetimeinterval",
}
MAGIC_LEVEL_KEYS = {
    "effect",
    "effectext",
    "effect2",
    "effect3",
    "effectmana",
    "lifemax",
    "thewmax",
    "manamax",
    "attack",
    "attack2",
    "attack3",
    "defend",
    "defend2",
    "defend3",
    "evade",
    "addthewrestorepercent",
    "addmanarestorepercent",
    "addliferestorepercent",
    "rangeaddlife",
    "rangeaddmana",
    "rangeaddthew",
    "rangeaddrage",
    "rangefreeze",
    "rangepoison",
    "rangepetrify",
    "rangedamage",
    "leaptimes",
    "leapframe",
    "effectreducepercentage",
    "levelupexp",
    "lifecost",
    "manacost",
    "thewcost",
    "ragecost",
    "critchanceaddvalue",
    "critdamageaddpercent",
    "count",
    "movekind",
    "specialkind",
    "specialkindvalue",
    "specialkindmilliseconds",
    "alphablend",
    "region",
    "speed",
    "flyinglum",
    "vanishlum",
    "waitframe",
    "lifeframe",
    "attackradius",
}
MAGIC_INIT_KEYS = MAGIC_GENERAL_KEYS | MAGIC_LEVEL_KEYS
MAGIC_SUPPORTED_MOVE_KINDS = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 16, 17, 19, 20, 21, 22, 23, 24}
MAGIC_SUPPORTED_SPECIAL_KINDS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 99}
MAGIC_SUPPORTED_REGIONS = {0, 1, 2, 3, 4, 5, 6}
RELATION_TABLE_SECTIONS = {"conquer", "match"}
RELATION_TABLE_KEYS = {f"league{index:02d}" for index in range(14)}
RELATION_TABLE_SUPPORTED_LEAGUES = set(range(-1, 14))
RELATION_TABLE_ENUM_RULES = {key: RELATION_TABLE_SUPPORTED_LEAGUES for key in RELATION_TABLE_KEYS}

GOODS_INIT_KEYS = {
    "name",
    "kind",
    "cost",
    "sellprice",
    "intro",
    "effect",
    "effecttype",
    "specialeffect",
    "specialeffectvalue",
    "fighterfriendhasdrugeffect",
    "followpartnerhasdrugeffect",
    "coldmilliseconds",
    "user",
    "minuserlevel",
    "sex",
    "changemovespeedpercent",
    "addmagiceffectpercent",
    "addmagiceffectamount",
    "addmagiceffectname",
    "addmagiceffecttype",
    "noneedtoequip",
    "magicname",
    "magiciniwhenuse",
    "replacemagic",
    "usereplacemagic",
    "flyini",
    "flyini2",
    "magictousewhenbeattacked",
    "magicdirectionwhenbeattacked",
    "image",
    "icon",
    "part",
    "script",
    "lifemax",
    "thewmax",
    "manamax",
    "life",
    "thew",
    "mana",
    "attack",
    "attack2",
    "attack3",
    "defend",
    "defend2",
    "defend3",
    "evade",
}
GOODS_LIST_HEADER_KEYS = {
    "count",
    "numbervalid",
    "buypercent",
    "recyclepercent",
}
GOODS_LIST_ITEM_KEYS = {
    "inifile",
    "number",
}
GOODS_SUPPORTED_KINDS = {0, 1, 2}
GOODS_RANDOM_INTEGER_KEYS = {
    "cost",
    "sellprice",
    "effecttype",
    "specialeffect",
    "specialeffectvalue",
    "fighterfriendhasdrugeffect",
    "followpartnerhasdrugeffect",
    "coldmilliseconds",
    "minuserlevel",
    "sex",
    "changemovespeedpercent",
    "addmagiceffectpercent",
    "addmagiceffectamount",
    "noneedtoequip",
    "magicdirectionwhenbeattacked",
    "lifemax",
    "thewmax",
    "manamax",
    "life",
    "thew",
    "mana",
    "attack",
    "attack2",
    "attack3",
    "defend",
    "defend2",
    "defend3",
    "evade",
}

NPC_HEAD_KEYS = {"map", "count"}
NPC_ENTITY_KEYS = {
    "name",
    "showname",
    "kind",
    "npcini",
    "sex",
    "dir",
    "mapx",
    "mapy",
    "action",
    "walkspeed",
    "standspeed",
    "attackspeed",
    "idle",
    "ai_type",
    "aitype",
    "group",
    "noautoattackplayer",
    "stopfindingtarget",
    "pathfinder",
    "dialogradius",
    "scriptfile",
    "scriptfileright",
    "timerscriptfile",
    "timerscriptinterval",
    "caninteractdirectly",
    "buyinifile",
    "dropini",
    "nodropwhendie",
    "keepattackx",
    "keepattacky",
    "revivemilliseconds",
    "leftmillisecondstorevive",
    "lifemilliseconds",
    "isbodyiniadded",
    "visiblevariablename",
    "visiblevariablevalue",
    "state",
    "relation",
    "kindvalue",
    "kindvaluemax",
    "talkcontent",
    "baggoods",
    "steal",
    "eloquence",
    "leechcraft",
    "autorunscript",
    "arm",
    "evaden",
    "gengu",
    "neixi",
    "physique",
    "issignalshow",
    "signalindex",
    "signaltype",
    "expbonus",
    "life",
    "lifemax",
    "thew",
    "thewmax",
    "mana",
    "manamax",
    "attack",
    "attack2",
    "attack3",
    "defend",
    "defence",
    "defend2",
    "defend3",
    "evade",
    "duck",
    "dodge_beginframe",
    "dodge_endframe",
    "exp",
    "levelupexp",
    "canlevelup",
    "level",
    "levelini",
    "poisonseconds",
    "poisonbycharactername",
    "petrifiedseconds",
    "frozenseconds",
    "ispoisionvisualeffect",
    "ispoisonvisualeffect",
    "ispetrifiedvisualeffect",
    "isfronzenvisualeffect",
    "isfrozenvisualeffect",
    "invincible",
    "fixedpos",
    "currpos",
    "currentfixedposindex",
    "destinationmapposx",
    "destinationmapposy",
    "attacklevel",
    "magiclevel",
    "canequip",
    "headequip",
    "neckequip",
    "bodyequip",
    "backequip",
    "handequip",
    "wristequip",
    "footequip",
    "backgroundtextureequip",
    "lum",
    "visionradius",
    "attackradius",
    "bodyini",
    "flyini",
    "flyini2",
    "flyinis",
    "magicini",
    "magictousewhenlifelow",
    "lifelowpercent",
    "keepradiuswhenlifelow",
    "keepradiuswhenfrienddeath",
    "magictousewhenbeattacked",
    "magicdirectionwhenbeattacked",
    "magictousewhendeath",
    "magicdirectionwhendeath",
    "addmovespeedpercent",
    "hurtplayerinterval",
    "hurtplayerlife",
    "hurtplayerradius",
    "deathscript",
}
PLAYER_SNAPSHOT_ENTITY_KEYS = NPC_ENTITY_KEYS | {
    "doing",
    "desx",
    "desy",
    "belong",
    "canrun",
    "canjump",
    "fight",
    "timelimit",
    "timetrigger",
    "timecount",
    "money",
    "magic",
    "justice",
    "emotion",
    "timescript",
    "secondattack",
}
PLAYER_SNAPSHOT_SIGNATURE_KEYS = {
    "doing",
    "desx",
    "desy",
    "canrun",
    "canjump",
    "fight",
    "money",
    "levelini",
}
NPC_SUPPORTED_KINDS = set(range(0, 8))
NPC_SUPPORTED_RELATIONS = {0, 1, 2, 3}
NPC_SUPPORTED_PATH_FINDERS = {0, 1}
NPC_SUPPORTED_ACTIONS = {0, 1, 2, 6}
NPC_RES_SUPPORTED_ACTION_SECTIONS = {
    "stand",
    "stand1",
    "walk",
    "run",
    "jump",
    "attack",
    "attack1",
    "attack2",
    "magic",
    "hurt",
    "death",
    "sit",
    "astand",
    "awalk",
    "arun",
    "ajump",
}
NPC_RES_ACTION_SECTION_ALIASES = {
    "fightstand": "astand",
    "fightwalk": "awalk",
    "fightrun": "arun",
    "fightjump": "ajump",
}
NPC_RES_DEFERRED_ACTION_SECTION_REASONS = {
    "attack3": "NPC resource Attack3 section is present in MOD data, but JxqyHD/newjx/miu2d/MG runtime action states only map Attack/Attack1/Attack2",
    "walk1": "NPC resource Walk1 section is present in MOD data, but no matching runtime state in JxqyHD/newjx/miu2d/MG has been confirmed",
    "fightstand1": "NPC resource FightStand1 section is present in MOD data, but JxqyHD/newjx/miu2d/MG only map FightStand/FightWalk/FightRun/FightJump",
}
NPC_RES_ACTION_SECTIONS = NPC_RES_SUPPORTED_ACTION_SECTIONS | set(NPC_RES_DEFERRED_ACTION_SECTION_REASONS)
NPC_RES_ACTION_KEYS = {"image", "shade", "sound"}
OBJECT_HEAD_KEYS = {"map", "count"}
OBJECT_ENTITY_KEYS = {
    "objname",
    "name",
    "objfile",
    "objfilemovie",
    "scriptfile",
    "scriptfileright",
    "timerscriptfile",
    "timerscriptinterval",
    "revivenpcini",
    "caninteractdirectly",
    "scriptfilejusttouch",
    "wavfile",
    "kind",
    "dir",
    "mapx",
    "mapy",
    "offsetx",
    "offsety",
    "offx",
    "offy",
    "lum",
    "damage",
    "frame",
    "height",
    "millisecondstoremove",
    "state",
    "actiontime",
}
OBJECT_ENTITY_SIGNATURE_KEYS = {
    "objname",
    "name",
    "objfile",
    "scriptfile",
    "scriptfileright",
    "wavfile",
    "kind",
    "dir",
    "mapx",
    "mapy",
    "offx",
    "offy",
    "offsetx",
    "offsety",
    "damage",
    "frame",
}
OBJECT_DROP_HEADER_KEYS = {"count"}
OBJECT_DROP_ITEM_KEYS = {
    "objfile",
    "num",
    "odds",
    "group",
}
OBJECT_RES_COMMON_KEYS = {
    "image",
    "shade",
    "sound",
    "animation",
}
OBJECT_SUPPORTED_KINDS = set(range(0, 9))
OBJECT_SUPPORTED_STATES = {0, 1, 2, 3}
IGNORED_EMPTY_FIELDS = {
    ("goods", "init", "imagepng"),
    ("magic", "init", "imagepng"),
}
DEFERRED_FIELDS = {
    ("npc", "entity", "doing"): "player snapshot field in NPC directory",
    ("npc", "entity", "desx"): "player snapshot field in NPC directory",
    ("npc", "entity", "desy"): "player snapshot field in NPC directory",
    ("npc", "entity", "belong"): "player/MOD custom field, no NPC runtime property confirmed",
    ("npc", "entity", "canrun"): "player snapshot field in NPC directory",
    ("npc", "entity", "canjump"): "player snapshot field in NPC directory",
    ("npc", "entity", "fight"): "player snapshot field in NPC directory",
    ("npc", "entity", "timelimit"): "player/global timer snapshot field in NPC directory",
    ("npc", "entity", "timetrigger"): "player/global timer snapshot field in NPC directory",
    ("npc", "entity", "timecount"): "player/global timer snapshot field in NPC directory",
    ("npc", "entity", "money"): "player snapshot field in NPC directory",
    ("npc", "entity", "magic"): "player snapshot field in NPC directory",
    ("npc", "entity", "justice"): "player snapshot field in NPC directory",
    ("npc", "entity", "emotion"): "player snapshot field in NPC directory",
    ("npc", "entity", "timescript"): "player/global timer snapshot field in NPC directory",
    ("npc", "entity", "secondattack"): "player snapshot field in NPC directory",
    ("object", "entity", "type"): "object editor/category metadata, no C# Obj property confirmed",
    ("object_res", "other", "objname"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "objfile"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "wavfile"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "scriptfile"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "kind"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "dir"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "lum"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "mapx"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "mapy"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "offx"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "offy"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "damage"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "frame"): "object entity fields in objres path, migration layout issue",
    ("object_res", "other", "image"): "malformed object resource section, migration layout issue",
    ("object_res", "other", "shade"): "malformed object resource section, migration layout issue",
    ("object_res", "other", "sound"): "malformed object resource section, migration layout issue",
}

MAGIC_ENUM_RULES = {
    "movekind": MAGIC_SUPPORTED_MOVE_KINDS,
    "specialkind": MAGIC_SUPPORTED_SPECIAL_KINDS,
    "region": MAGIC_SUPPORTED_REGIONS,
}
GOODS_ENUM_RULES = {
    "kind": GOODS_SUPPORTED_KINDS,
}
NPC_ENUM_RULES = {
    "kind": NPC_SUPPORTED_KINDS,
    "relation": NPC_SUPPORTED_RELATIONS,
    "pathfinder": NPC_SUPPORTED_PATH_FINDERS,
    "action": NPC_SUPPORTED_ACTIONS,
    "lum": {0, 1, 2, 3, 4, 5},
}
OBJECT_ENUM_RULES = {
    "kind": OBJECT_SUPPORTED_KINDS,
    "state": OBJECT_SUPPORTED_STATES,
    "lum": {0, 1, 2, 3, 4, 5},
}


@dataclass(frozen=True)
class Location:
    file: str
    line: int
    section: str
    text: str


@dataclass(frozen=True)
class DataFile:
    category: str
    path: Path
    root: Path


def normalize_name(name: str) -> str:
    return name.strip().lower()


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def read_text_best_effort(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-8", "gbk", "cp950"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def relative_file(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root.resolve())).replace("\\", "/")
    except ValueError:
        return str(path)


def is_comment_line(line: str) -> bool:
    stripped = line.lstrip("\ufeff").lstrip()
    return stripped.startswith("//") or stripped.startswith("#") or stripped.startswith(";")


def strip_inline_comment(value: str) -> str:
    in_single_quote = False
    in_double_quote = False
    for index, char in enumerate(value):
        if char == "'" and not in_double_quote:
            in_single_quote = not in_single_quote
        elif char == '"' and not in_single_quote:
            in_double_quote = not in_double_quote
        elif char in {";", "#"} and not in_single_quote and not in_double_quote:
            return value[:index]
    return value


def parse_int_value(value: str) -> int | None:
    value = strip_inline_comment(value).strip()
    if not value:
        return None
    match = re.match(r"^[+-]?(?:0[xX][0-9A-Fa-f]+|\d+)", value)
    if match is None:
        return None
    try:
        return int(match.group(0), 0)
    except ValueError:
        return None


def is_random_integer_value(value: str) -> bool:
    value = strip_inline_comment(value).strip()
    if not value:
        return False
    if ">" in value:
        parts = value.split(">", 1)
        return parse_int_value(parts[0]) is not None and parse_int_value(parts[1]) is not None
    if "," in value:
        parts = [part.strip() for part in value.split(",") if part.strip()]
        return len(parts) > 1 and all(parse_int_value(part) is not None for part in parts)
    return False


def is_npc_entity_section(section: str) -> bool:
    normalized = normalize_name(section)
    if normalized in {"head", "init"}:
        return True
    return re.match(r"^npc\d+$", normalized) is not None


def is_npc_resource_action_section(section: str) -> bool:
    return normalize_name(section) in NPC_RES_ACTION_SECTIONS


def is_npc_resource_like_text(text: str) -> bool:
    current_section = ""
    current_section_is_action = False
    has_action_section = False
    has_action_resource_key = False
    for raw_line in text.splitlines():
        line = raw_line.lstrip("\ufeff")
        if is_comment_line(line):
            continue
        section_match = SECTION_RE.match(line)
        if section_match:
            current_section = section_match.group(1).strip()
            if is_npc_entity_section(current_section):
                return False
            current_section_is_action = is_npc_resource_action_section(current_section)
            has_action_section = has_action_section or current_section_is_action
            continue
        key_match = KEY_VALUE_RE.match(line)
        if key_match is None or not current_section_is_action:
            continue
        key = normalize_name(key_match.group(1))
        if key in NPC_RES_ACTION_KEYS:
            has_action_resource_key = True
    return has_action_section and has_action_resource_key


def is_relation_table_like_text(text: str) -> bool:
    current_section = ""
    has_relation_section = False
    has_relation_key = False
    for raw_line in text.splitlines():
        line = raw_line.lstrip("\ufeff")
        if is_comment_line(line):
            continue
        section_match = SECTION_RE.match(line)
        if section_match:
            current_section = normalize_name(section_match.group(1))
            has_relation_section = has_relation_section or current_section in RELATION_TABLE_SECTIONS
            continue
        key_match = KEY_VALUE_RE.match(line)
        if key_match is None or current_section not in RELATION_TABLE_SECTIONS:
            continue
        key = normalize_name(key_match.group(1))
        if key in RELATION_TABLE_KEYS:
            has_relation_key = True
    return has_relation_section and has_relation_key


def is_player_snapshot_like_text(text: str) -> bool:
    current_section = ""
    signature_keys: set[str] = set()
    for raw_line in text.splitlines():
        line = raw_line.lstrip("\ufeff")
        if is_comment_line(line):
            continue
        section_match = SECTION_RE.match(line)
        if section_match:
            current_section = normalize_name(section_match.group(1))
            continue
        if current_section != "init":
            continue
        key_match = KEY_VALUE_RE.match(line)
        if key_match is None:
            continue
        key = normalize_name(key_match.group(1))
        if key in PLAYER_SNAPSHOT_SIGNATURE_KEYS:
            signature_keys.add(key)
    return len(signature_keys) >= 4


def has_path_segment_sequence(path: Path, sequence: tuple[str, ...]) -> bool:
    parts = [part.lower() for part in path.parts]
    for index in range(0, len(parts) - len(sequence) + 1):
        if tuple(parts[index:index + len(sequence)]) == sequence:
            return True
    return False


def classify_data_file(path: Path) -> str | None:
    suffix = path.suffix.lower()
    if suffix == ".npc":
        return "npc"
    if suffix == ".obj":
        return "object"
    if suffix != ".ini":
        return None
    if has_path_segment_sequence(path, ("ini", "magic")):
        if path.name.lower() == "relation.ini" and is_relation_table_like_text(read_text_best_effort(path)):
            return "relation_table"
        return "magic"
    if has_path_segment_sequence(path, ("ini", "goods")):
        return "goods"
    if has_path_segment_sequence(path, ("ini", "npcres")):
        return "npc_res"
    if has_path_segment_sequence(path, ("ini", "npc")):
        text = read_text_best_effort(path)
        if is_npc_resource_like_text(text):
            return "npc_res"
        if is_player_snapshot_like_text(text):
            return "player_snapshot"
        return "npc"
    if has_path_segment_sequence(path, ("ini", "objres")):
        return "object_res"
    if has_path_segment_sequence(path, ("ini", "obj")):
        return "object"
    return None


def is_runtime_generated_path(path: Path) -> bool:
    return any(part.lower() in RUNTIME_GENERATED_DIRS for part in path.parts)


def iter_data_files(root: Path) -> Iterable[DataFile]:
    root = root.resolve()
    if root.is_file():
        if is_runtime_generated_path(root):
            return
        category = classify_data_file(root)
        if category is not None:
            yield DataFile(category, root, root.parent)
        return

    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if is_runtime_generated_path(path):
            continue
        category = classify_data_file(path)
        if category is not None:
            yield DataFile(category, path, root)


def deduplicate_files(files: Iterable[DataFile]) -> list[DataFile]:
    deduplicated: dict[Path, DataFile] = {}
    for data_file in files:
        resolved = data_file.path.resolve()
        existing = deduplicated.get(resolved)
        if existing is None or len(data_file.root.parts) > len(existing.root.parts):
            deduplicated[resolved] = data_file
    return sorted(deduplicated.values(), key=lambda item: str(item.path).lower())


def section_kind_for_magic(section: str) -> str:
    normalized = normalize_name(section)
    if normalized == "init":
        return "init"
    if LEVEL_SECTION_RE.match(normalized):
        return "level"
    return "other"


def section_kind_for_relation_table(section: str) -> str:
    normalized = normalize_name(section)
    if normalized in RELATION_TABLE_SECTIONS:
        return normalized
    return "other"


def section_kind_for_goods(section: str) -> str:
    normalized = normalize_name(section)
    if normalized == "init":
        return "init"
    if normalized in {"header", "head"}:
        return "list_header"
    if re.match(r"^\d+$", normalized):
        return "list_item"
    return "other"


def section_kind_for_npc(section: str) -> str:
    normalized = normalize_name(section)
    if normalized == "head":
        return "head"
    if normalized == "init":
        return "entity"
    if re.match(r"^npc\d+$", normalized):
        return "entity"
    return "other"


def section_kind_for_player_snapshot(section: str) -> str:
    normalized = normalize_name(section)
    if normalized == "init":
        return "entity"
    return "other"


def section_kind_for_npc_res(section: str) -> str:
    raw = normalize_name(section)
    normalized = NPC_RES_ACTION_SECTION_ALIASES.get(raw, raw)
    if normalized in NPC_RES_ACTION_SECTIONS:
        return normalized
    return "other"


def section_kind_for_object(section: str) -> str:
    normalized = normalize_name(section)
    if normalized == "head":
        return "head"
    if normalized == "init" or re.match(r"^obj\d+$", normalized):
        return "entity"
    return "other"


def section_kind_for_object_drop_table(section: str) -> str:
    normalized = normalize_name(section)
    if normalized == "init":
        return "drop_header"
    if re.match(r"^\d+$", normalized):
        return "drop_item"
    return section_kind_for_object(section)


def section_kind_for_object_res(section: str) -> str:
    normalized = normalize_name(section)
    if normalized == "common":
        return "common"
    return "other"


def is_supported_field(category: str, section_kind: str, key: str) -> bool:
    if category == "magic":
        if section_kind == "init":
            return key in MAGIC_INIT_KEYS
        if section_kind == "level":
            return key in MAGIC_LEVEL_KEYS
        return False
    if category == "relation_table":
        if section_kind in RELATION_TABLE_SECTIONS:
            return key in RELATION_TABLE_KEYS
        return False
    if category == "goods":
        if section_kind == "init":
            return key in GOODS_INIT_KEYS
        if section_kind == "list_header":
            return key in GOODS_LIST_HEADER_KEYS
        if section_kind == "list_item":
            return key in GOODS_LIST_ITEM_KEYS
        return False
    if category == "npc":
        if section_kind == "head":
            return key in NPC_HEAD_KEYS
        if section_kind == "entity":
            return key in NPC_ENTITY_KEYS
        return False
    if category == "player_snapshot":
        if section_kind == "entity":
            return key in PLAYER_SNAPSHOT_ENTITY_KEYS
        return False
    if category == "npc_res":
        if section_kind in NPC_RES_SUPPORTED_ACTION_SECTIONS:
            return key in NPC_RES_ACTION_KEYS
        return False
    if category == "object":
        if section_kind == "head":
            return key in OBJECT_HEAD_KEYS
        if section_kind == "entity":
            return key in OBJECT_ENTITY_KEYS
        if section_kind == "drop_header":
            return key in OBJECT_DROP_HEADER_KEYS
        if section_kind == "drop_item":
            return key in OBJECT_DROP_ITEM_KEYS
        return False
    if category == "object_res":
        if section_kind == "common":
            return key in OBJECT_RES_COMMON_KEYS
        return False
    return False


def should_ignore_empty_field(category: str, section_kind: str, key: str, value: str) -> bool:
    if (category, section_kind, key) not in IGNORED_EMPTY_FIELDS:
        return False
    return strip_inline_comment(value).strip() == ""


def deferred_field_reason(category: str, section_kind: str, key: str) -> str | None:
    if (
        category == "npc_res"
        and section_kind in NPC_RES_DEFERRED_ACTION_SECTION_REASONS
        and key in NPC_RES_ACTION_KEYS
    ):
        return NPC_RES_DEFERRED_ACTION_SECTION_REASONS[section_kind]
    return DEFERRED_FIELDS.get((category, section_kind, key))


def section_kind_for(category: str, section: str) -> str:
    if category == "magic":
        return section_kind_for_magic(section)
    if category == "relation_table":
        return section_kind_for_relation_table(section)
    if category == "goods":
        return section_kind_for_goods(section)
    if category == "npc":
        return section_kind_for_npc(section)
    if category == "player_snapshot":
        return section_kind_for_player_snapshot(section)
    if category == "npc_res":
        return section_kind_for_npc_res(section)
    if category == "object":
        return section_kind_for_object(section)
    if category == "object_res":
        return section_kind_for_object_res(section)
    return "other"


def is_object_drop_table(text: str) -> bool:
    current_section = ""
    has_init_count = False
    has_numeric_obj_file = False
    for raw_line in text.splitlines():
        line = raw_line.lstrip("\ufeff")
        if is_comment_line(line):
            continue
        section_match = SECTION_RE.match(line)
        if section_match:
            current_section = normalize_name(section_match.group(1))
            continue
        key_match = KEY_VALUE_RE.match(line)
        if key_match is None:
            continue
        key = normalize_name(key_match.group(1))
        if current_section == "init" and key == "count":
            has_init_count = True
        elif re.match(r"^\d+$", current_section) and key == "objfile":
            has_numeric_obj_file = True
    return has_init_count and has_numeric_obj_file


def enum_rules_for(category: str, section_kind: str) -> dict[str, set[int]]:
    if category == "magic" and section_kind in {"init", "level"}:
        return MAGIC_ENUM_RULES
    if category == "relation_table" and section_kind in RELATION_TABLE_SECTIONS:
        return RELATION_TABLE_ENUM_RULES
    if category == "goods" and section_kind == "init":
        return GOODS_ENUM_RULES
    if category == "npc" and section_kind == "entity":
        return NPC_ENUM_RULES
    if category == "player_snapshot" and section_kind == "entity":
        return NPC_ENUM_RULES
    if category == "object" and section_kind == "entity":
        return OBJECT_ENUM_RULES
    return {}


def should_flag_enum_value(category: str, key: str, value: int, supported_values: set[int]) -> bool:
    if category == "magic" and key == "region" and value == 0:
        return False
    if category == "npc" and key == "lum":
        return value < 0
    if category == "object" and key == "lum":
        return value < 0
    return value not in supported_values


def scan_data_file(
    data_file: DataFile,
    repo_root: Path,
    max_examples: int,
    category_reports: dict[str, dict[str, object]],
) -> None:
    report = category_reports[data_file.category]
    report["fileCount"] += 1

    current_section = ""
    section_kind = "other"
    text = read_text_best_effort(data_file.path)
    object_drop_table = data_file.category == "object" and is_object_drop_table(text)
    file_label = relative_file(data_file.path, repo_root)
    object_res_entity_field_location: Location | None = None
    object_res_common_key_outside_common_location: Location | None = None
    object_res_malformed_common_header_location: Location | None = None
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.lstrip("\ufeff")
        if is_comment_line(line):
            continue
        section_match = SECTION_RE.match(line)
        if (
            data_file.category == "object_res"
            and section_match is None
            and "[common]" in line.lower()
        ):
            object_res_malformed_common_header_location = object_res_malformed_common_header_location or Location(
                file_label,
                line_number,
                current_section,
                raw_line.strip(),
            )
        if section_match:
            current_section = section_match.group(1).strip()
            if object_drop_table:
                section_kind = section_kind_for_object_drop_table(current_section)
            else:
                section_kind = section_kind_for(data_file.category, current_section)
            report["sectionKinds"][section_kind] += 1
            continue

        key_match = KEY_VALUE_RE.match(line)
        if key_match is None:
            continue

        key = normalize_name(key_match.group(1))
        value = key_match.group(2).strip()
        location = Location(file_label, line_number, current_section, raw_line.strip())
        if data_file.category == "object_res" and section_kind != "common":
            if key in OBJECT_ENTITY_SIGNATURE_KEYS:
                object_res_entity_field_location = object_res_entity_field_location or location
            if key in OBJECT_RES_COMMON_KEYS:
                object_res_common_key_outside_common_location = object_res_common_key_outside_common_location or location
        report["fieldCount"] += 1
        report["fields"][key] += 1
        report["sectionKindFields"][(section_kind, key)] += 1
        if data_file.category == "goods" and section_kind == "init" and key in GOODS_RANDOM_INTEGER_KEYS and is_random_integer_value(value):
            random_key = f"{section_kind}.{key}"
            report["randomIntegerFields"][random_key] += 1
            if len(report["randomIntegerFieldLocations"][random_key]) < max_examples:
                report["randomIntegerFieldLocations"][random_key].append(location)

        if should_ignore_empty_field(data_file.category, section_kind, key, value):
            ignored_key = f"{section_kind}.{key}"
            report["ignoredEmptyFields"][ignored_key] += 1
            if len(report["ignoredEmptyFieldLocations"][ignored_key]) < max_examples:
                report["ignoredEmptyFieldLocations"][ignored_key].append(location)
        else:
            deferred_reason = deferred_field_reason(data_file.category, section_kind, key)
            if deferred_reason is not None:
                deferred_key = f"{section_kind}.{key}"
                report["deferredFields"][deferred_key] += 1
                report["deferredFieldReasons"][deferred_key] = deferred_reason
                if len(report["deferredFieldLocations"][deferred_key]) < max_examples:
                    report["deferredFieldLocations"][deferred_key].append(location)
            elif not is_supported_field(data_file.category, section_kind, key):
                unknown_key = f"{section_kind}.{key}"
                report["unknownFields"][unknown_key] += 1
                if len(report["unknownFieldLocations"][unknown_key]) < max_examples:
                    report["unknownFieldLocations"][unknown_key].append(location)
        enum_rules = enum_rules_for(data_file.category, section_kind)
        if key not in enum_rules:
            continue

        enum_value = parse_int_value(value)
        enum_key = key
        if enum_value is None:
            report["nonNumericEnumValues"][enum_key] += 1
            if len(report["nonNumericEnumLocations"][enum_key]) < max_examples:
                report["nonNumericEnumLocations"][enum_key].append(location)
            continue

        enum_value_key = f"{enum_key}={enum_value}"
        report["enumValues"][enum_value_key] += 1
        if len(report["enumValueLocations"][enum_value_key]) < max_examples:
            report["enumValueLocations"][enum_value_key].append(location)
        if should_flag_enum_value(data_file.category, key, enum_value, enum_rules[key]):
            report["unsupportedEnumValues"][enum_value_key] += 1
            if len(report["unsupportedEnumLocations"][enum_value_key]) < max_examples:
                report["unsupportedEnumLocations"][enum_value_key].append(location)

    if data_file.category == "object_res":
        if object_res_entity_field_location is not None:
            structural_key = "object_res.entity_fields_in_objres"
            report["structuralIssues"][structural_key] += 1
            if len(report["structuralIssueLocations"][structural_key]) < max_examples:
                report["structuralIssueLocations"][structural_key].append(object_res_entity_field_location)
        if object_res_common_key_outside_common_location is not None:
            structural_key = "object_res.common_keys_outside_common_section"
            report["structuralIssues"][structural_key] += 1
            if len(report["structuralIssueLocations"][structural_key]) < max_examples:
                report["structuralIssueLocations"][structural_key].append(object_res_common_key_outside_common_location)
        if object_res_malformed_common_header_location is not None:
            structural_key = "object_res.malformed_common_header"
            report["structuralIssues"][structural_key] += 1
            if len(report["structuralIssueLocations"][structural_key]) < max_examples:
                report["structuralIssueLocations"][structural_key].append(object_res_malformed_common_header_location)


def make_empty_category_report() -> dict[str, object]:
    return {
        "fileCount": 0,
        "fieldCount": 0,
        "sectionKinds": Counter(),
        "fields": Counter(),
        "sectionKindFields": Counter(),
        "unknownFields": Counter(),
        "unknownFieldLocations": defaultdict(list),
        "deferredFields": Counter(),
        "deferredFieldLocations": defaultdict(list),
        "deferredFieldReasons": {},
        "ignoredEmptyFields": Counter(),
        "ignoredEmptyFieldLocations": defaultdict(list),
        "enumValues": Counter(),
        "enumValueLocations": defaultdict(list),
        "unsupportedEnumValues": Counter(),
        "unsupportedEnumLocations": defaultdict(list),
        "nonNumericEnumValues": Counter(),
        "nonNumericEnumLocations": defaultdict(list),
        "randomIntegerFields": Counter(),
        "randomIntegerFieldLocations": defaultdict(list),
        "structuralIssues": Counter(),
        "structuralIssueLocations": defaultdict(list),
    }


def summarize_counter(
    counter: Counter[str],
    locations: dict[str, list[Location]] | None = None,
    top_limit: int | None = None,
) -> list[dict[str, object]]:
    items = counter.most_common(top_limit)
    result = []
    for key, count in items:
        item: dict[str, object] = {"key": key, "count": count}
        if locations is not None:
            item["locations"] = [location.__dict__ for location in locations.get(key, [])]
        result.append(item)
    return result


def summarize_deferred_fields(
    counter: Counter[str],
    locations: dict[str, list[Location]],
    reasons: dict[str, str],
    top_limit: int | None = None,
) -> list[dict[str, object]]:
    result = summarize_counter(counter, locations, top_limit)
    for item in result:
        item["reason"] = reasons.get(str(item["key"]), "")
    return result


def summarize_enum(counter: Counter[str], locations: dict[str, list[Location]]) -> list[dict[str, object]]:
    grouped: dict[str, list[tuple[int, str, int]]] = defaultdict(list)
    for key, count in counter.items():
        enum_name, _, value_text = key.partition("=")
        try:
            value = int(value_text)
        except ValueError:
            value = 0
        grouped[enum_name].append((value, key, count))

    result = []
    for enum_name in sorted(grouped):
        values = []
        for value, key, count in sorted(grouped[enum_name], key=lambda item: item[0]):
            values.append(
                {
                    "value": value,
                    "count": count,
                    "locations": [location.__dict__ for location in locations.get(key, [])],
                }
            )
        result.append({"field": enum_name, "values": values})
    return result


def serialize_category_report(report: dict[str, object], top_limit: int) -> dict[str, object]:
    section_kind_fields = Counter({
        f"{section}.{key}": count
        for (section, key), count in report["sectionKindFields"].items()
    })
    return {
        "fileCount": report["fileCount"],
        "fieldCount": report["fieldCount"],
        "sectionKinds": dict(sorted(report["sectionKinds"].items())),
        "topFields": summarize_counter(report["fields"], top_limit=top_limit),
        "topSectionKindFields": summarize_counter(section_kind_fields, top_limit=top_limit),
        "unknownFields": summarize_counter(
            report["unknownFields"],
            report["unknownFieldLocations"],
            top_limit,
        ),
        "deferredFields": summarize_deferred_fields(
            report["deferredFields"],
            report["deferredFieldLocations"],
            report["deferredFieldReasons"],
            top_limit,
        ),
        "ignoredEmptyFields": summarize_counter(
            report["ignoredEmptyFields"],
            report["ignoredEmptyFieldLocations"],
            top_limit,
        ),
        "enumValues": summarize_enum(report["enumValues"], report["enumValueLocations"]),
        "unsupportedEnumValues": summarize_counter(
            report["unsupportedEnumValues"],
            report["unsupportedEnumLocations"],
            top_limit,
        ),
        "nonNumericEnumValues": summarize_counter(
            report["nonNumericEnumValues"],
            report["nonNumericEnumLocations"],
            top_limit,
        ),
        "randomIntegerFields": summarize_counter(
            report["randomIntegerFields"],
            report["randomIntegerFieldLocations"],
            top_limit,
        ),
        "structuralIssues": summarize_counter(
            report["structuralIssues"],
            report["structuralIssueLocations"],
            top_limit,
        ),
    }


def build_report(args: argparse.Namespace) -> dict[str, object]:
    repo_root = Path(args.repo_root).resolve()
    input_roots = [Path(path).resolve() for path in args.paths]
    data_files = deduplicate_files(
        data_file
        for input_root in input_roots
        for data_file in iter_data_files(input_root)
    )

    category_reports = {
        "magic": make_empty_category_report(),
        "relation_table": make_empty_category_report(),
        "goods": make_empty_category_report(),
        "npc": make_empty_category_report(),
        "player_snapshot": make_empty_category_report(),
        "npc_res": make_empty_category_report(),
        "object": make_empty_category_report(),
        "object_res": make_empty_category_report(),
    }
    for data_file in data_files:
        scan_data_file(data_file, repo_root, args.max_examples, category_reports)

    serialized_categories = {
        category: serialize_category_report(report, args.top)
        for category, report in category_reports.items()
    }
    return {
        "repoRoot": str(repo_root),
        "inputRoots": [str(path) for path in input_roots],
        "dataFileCount": len(data_files),
        "categories": serialized_categories,
        "supportedFields": {
            "magic": {
                "init": sorted(MAGIC_INIT_KEYS),
                "level": sorted(MAGIC_LEVEL_KEYS),
                "moveKind": sorted(MAGIC_SUPPORTED_MOVE_KINDS),
                "specialKind": sorted(MAGIC_SUPPORTED_SPECIAL_KINDS),
                "region": sorted(MAGIC_SUPPORTED_REGIONS),
            },
            "relationTable": {
                "sections": sorted(RELATION_TABLE_SECTIONS),
                "fields": sorted(RELATION_TABLE_KEYS),
                "leagueValues": sorted(RELATION_TABLE_SUPPORTED_LEAGUES),
            },
            "goods": {
                "init": sorted(GOODS_INIT_KEYS),
                "listHeader": sorted(GOODS_LIST_HEADER_KEYS),
                "listItem": sorted(GOODS_LIST_ITEM_KEYS),
                "kind": sorted(GOODS_SUPPORTED_KINDS),
            },
            "npc": {
                "head": sorted(NPC_HEAD_KEYS),
                "entity": sorted(NPC_ENTITY_KEYS),
                "kind": sorted(NPC_SUPPORTED_KINDS),
                "relation": sorted(NPC_SUPPORTED_RELATIONS),
                "pathFinder": sorted(NPC_SUPPORTED_PATH_FINDERS),
                "action": sorted(NPC_SUPPORTED_ACTIONS),
            },
            "playerSnapshot": {
                "entity": sorted(PLAYER_SNAPSHOT_ENTITY_KEYS),
                "signature": sorted(PLAYER_SNAPSHOT_SIGNATURE_KEYS),
                "kind": sorted(NPC_SUPPORTED_KINDS),
                "relation": sorted(NPC_SUPPORTED_RELATIONS),
                "pathFinder": sorted(NPC_SUPPORTED_PATH_FINDERS),
                "action": sorted(NPC_SUPPORTED_ACTIONS),
            },
            "npcRes": {
                "actionKeys": sorted(NPC_RES_ACTION_KEYS),
                "actionSections": sorted(NPC_RES_SUPPORTED_ACTION_SECTIONS),
                "actionSectionAliases": dict(sorted(NPC_RES_ACTION_SECTION_ALIASES.items())),
                "knownActionSections": sorted(NPC_RES_ACTION_SECTIONS),
                "deferredActionSections": sorted(NPC_RES_DEFERRED_ACTION_SECTION_REASONS),
            },
            "object": {
                "head": sorted(OBJECT_HEAD_KEYS),
                "entity": sorted(OBJECT_ENTITY_KEYS),
                "dropHeader": sorted(OBJECT_DROP_HEADER_KEYS),
                "dropItem": sorted(OBJECT_DROP_ITEM_KEYS),
                "kind": sorted(OBJECT_SUPPORTED_KINDS),
                "state": sorted(OBJECT_SUPPORTED_STATES),
            },
            "objectRes": {
                "common": sorted(OBJECT_RES_COMMON_KEYS),
            },
        },
    }


def format_locations(locations: list[dict[str, object]]) -> str:
    if not locations:
        return ""
    first = locations[0]
    return f" first: {first['file']}:{first['line']}"


def format_issue_list(items: list[dict[str, object]], empty_text: str = "None") -> list[str]:
    if not items:
        return [f"- {empty_text}"]
    return [
        f"- `{item['key']}` ({item['count']}){format_locations(item.get('locations', []))}"
        for item in items
    ]


def format_deferred_list(items: list[dict[str, object]], empty_text: str = "None") -> list[str]:
    if not items:
        return [f"- {empty_text}"]
    return [
        f"- `{item['key']}` ({item['count']}) - {item.get('reason', '')}{format_locations(item.get('locations', []))}"
        for item in items
    ]


def format_enum_values(enum_values: list[dict[str, object]]) -> list[str]:
    if not enum_values:
        return ["- None"]
    lines = []
    for item in enum_values:
        values = ", ".join(f"{value['value']} ({value['count']})" for value in item["values"])
        lines.append(f"- `{item['field']}`: {values}")
    return lines


def format_markdown(report: dict[str, object], top_limit: int) -> str:
    categories = report["categories"]
    lines = [
        "# MOD Data Compatibility Audit",
        "",
        f"- Data files: {report['dataFileCount']}",
        f"- Magic files: {categories['magic']['fileCount']}",
        f"- Relation table files: {categories['relation_table']['fileCount']}",
        f"- Goods files: {categories['goods']['fileCount']}",
        f"- NPC files: {categories['npc']['fileCount']}",
        f"- Player snapshot files: {categories['player_snapshot']['fileCount']}",
        f"- NPC resource files: {categories['npc_res']['fileCount']}",
        f"- Object files: {categories['object']['fileCount']}",
        f"- Object resource files: {categories['object_res']['fileCount']}",
        "",
        "## Input Roots",
    ]
    lines.extend(f"- {path}" for path in report["inputRoots"])

    for category in ("magic", "relation_table", "goods", "npc", "player_snapshot", "npc_res", "object", "object_res"):
        category_report = categories[category]
        title = {
            "magic": "Magic",
            "relation_table": "Relation Tables",
            "goods": "Goods",
            "npc": "NPC",
            "player_snapshot": "Player Snapshots",
            "npc_res": "NPC Resources",
            "object": "Object",
            "object_res": "Object Resources",
        }[category]
        lines.extend(
            [
                "",
                f"## {title}",
                f"- Files: {category_report['fileCount']}",
                f"- Fields: {category_report['fieldCount']}",
                f"- Section kinds: {category_report['sectionKinds']}",
                "",
                "### Unsupported Enum Values",
            ]
        )
        lines.extend(format_issue_list(category_report["unsupportedEnumValues"]))
        lines.extend(["", "### Non-numeric Enum Values"])
        lines.extend(format_issue_list(category_report["nonNumericEnumValues"]))
        lines.extend(["", "### Enum Value Distribution"])
        lines.extend(format_enum_values(category_report["enumValues"]))
        lines.extend(["", "### Unknown Fields"])
        lines.extend(format_issue_list(category_report["unknownFields"]))
        lines.extend(["", "### Deferred / Registered Fields"])
        lines.extend(format_deferred_list(category_report["deferredFields"]))
        lines.extend(["", "### Structural Issues"])
        lines.extend(format_issue_list(category_report["structuralIssues"]))
        lines.extend(["", "### Ignored Empty Fields"])
        lines.extend(format_issue_list(category_report["ignoredEmptyFields"]))
        if category == "goods":
            lines.extend(["", "### Random Integer Fields"])
            lines.extend(format_issue_list(category_report["randomIntegerFields"]))
        lines.extend(["", f"### Top {min(top_limit, len(category_report['topFields']))} Fields"])
        lines.extend(
            f"- `{item['key']}` ({item['count']})"
            for item in category_report["topFields"][:top_limit]
        )

    return "\n".join(lines) + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Scan converted MOD magic/goods/NPC/object data and compare fields with current C++ loaders.",
    )
    parser.add_argument(
        "paths",
        nargs="*",
        default=["assets"],
        help="Resource pack, assets collection, or data file paths. Defaults to assets.",
    )
    parser.add_argument("--repo-root", default=".", help="Repository root. Defaults to current directory.")
    parser.add_argument("--json", dest="json_path", help="Write full JSON report to this path.")
    parser.add_argument("--markdown", dest="markdown_path", help="Write Markdown summary to this path.")
    parser.add_argument("--top", type=int, default=40, help="Number of top/list entries in text output.")
    parser.add_argument(
        "--max-examples",
        type=int,
        default=5,
        help="Maximum locations retained per field/value in the report.",
    )
    parser.add_argument(
        "--fail-on-unsupported-enum",
        action="store_true",
        help="Exit 1 when unsupported enum values are found.",
    )
    parser.add_argument(
        "--fail-on-structural-issues",
        action="store_true",
        help="Exit 1 when migration/layout structural issues are found.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    report = build_report(args)
    markdown = format_markdown(report, args.top)
    print(markdown, end="")

    if args.json_path:
        Path(args.json_path).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    if args.markdown_path:
        Path(args.markdown_path).write_text(markdown, encoding="utf-8")

    if args.fail_on_unsupported_enum:
        categories = report["categories"]
        if any(categories[category]["unsupportedEnumValues"] for category in categories):
            return 1
    if args.fail_on_structural_issues:
        categories = report["categories"]
        if any(categories[category]["structuralIssues"] for category in categories):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
