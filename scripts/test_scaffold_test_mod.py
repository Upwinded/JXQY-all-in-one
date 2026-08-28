#!/usr/bin/env python3
"""Focused regression tests for generated test MOD script scenarios."""

from __future__ import annotations

import tempfile
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest.mock import patch

import scaffold_test_mod as scaffold_module

from scaffold_test_mod import (
    REPO_ROOT,
    discover_base_pack,
    make_choose_ex_plus_script,
    make_choose_menu_visual_script,
    make_default_drop_pickup_script,
    make_dice_script,
    make_fish_script,
    make_gamble_script,
    make_goods_lifecycle_case_ini,
    make_goods_lifecycle_script,
    make_goods_random_ini,
    make_goods_random_script,
    make_equipment_trigger_script,
    make_magic_attack_all_leap_ini,
    make_magic_attack_all_projectile_ini,
    make_magic_attack_all_trace_enemy_ini,
    make_magic_begin_follow_ini,
    make_magic_body_medium_ini,
    make_magic_bounce_handoff_ini,
    make_magic_bouncefly_handoff_ini,
    make_magic_collision_lethal_freeze_ini,
    make_magic_collision_friend_npc_ini,
    make_magic_collision_partner_npc_ini,
    make_magic_collision_script,
    make_magic_carry_user1_hidden_ini,
    make_magic_carry_user4_hidden_ini,
    make_magic_control_ini,
    make_magic_control_target_npc_ini,
    make_magic_control_watcher_npc_ini,
    make_magic_damage_channels_ini,
    make_magic_damage_channels_target_npc_ini,
    make_magic_explode_child_ini,
    make_magic_explode_point_parent_ini,
    make_magic_explode_script,
    make_magic_explode_throw_child_ini,
    make_magic_explode_throw_parent_ini,
    make_magic_explode_throw_suppressed_parent_ini,
    make_magic_equipment_additional_freeze_ini,
    make_magic_ai_friend_death_attack_ini,
    make_magic_leap_ini,
    make_magic_lifecycle_script,
    make_manual_magic_arena_script,
    make_manual_magic_arena_target_npc_ini,
    make_manual_magic_arena_trainer_npc_ini,
    make_manual_magic_arena_trainer_script,
    make_magic_meteor_ini,
    make_magic_morph_replace_ini,
    make_magic_shop_death_kill_ini,
    make_magic_time_stop_visual_ini,
    make_magic_post_cast_jump_end_ini,
    make_magic_post_cast_die_ini,
    make_magic_post_cast_parent_ini,
    make_magic_post_cast_rand_ini,
    make_magic_post_cast_script,
    make_magic_post_cast_second_ini,
    make_magic_pass_through_ini,
    make_magic_pass_through_wall_ini,
    make_magic_parasitic_ini,
    make_magic_partner_projectile_ini,
    make_magic_range_attack_all_ini,
    make_magic_range_attack_ini,
    make_magic_random_move_ini,
    make_magic_round_ini,
    make_magic_summon_body_script,
    make_magic_summon_ini,
    make_magic_summon_npc_ini,
    make_magic_trace_enemy_ini,
    make_magic_trace_nonfighter_npc_ini,
    make_magic_transport_control_script,
    make_magic_trail_ini,
    make_magic_trail_script,
    make_npc_ai_flyer_ini,
    make_npc_ai_friend_attacker_ini,
    make_npc_ai_friend_none_target_ini,
    make_npc_ai_friend_victim_ini,
    make_npc_ai_friend_watcher_ini,
    make_npc_ai_none_fighter_ini,
    make_npc_ai_noadd_body_ini,
    make_npc_ai_path_best_ini,
    make_npc_ai_path_enemy_ini,
    make_npc_ai_path_event_ini,
    make_npc_ai_path_fixed_ini,
    make_npc_ai_path_normal_ini,
    make_npc_ai_state_ini,
    make_npc_ai_timer_context_script,
    make_npc_ai_script,
    make_npc_default_drop_boss_ini,
    make_npc_drop_fixture_ini,
    make_npc_drop_script,
    make_npc_drop_table_ini,
    make_npc_drop_token_ini,
    make_npc_shop_death_ini,
    make_object_alias_delete_current_script,
    make_object_interact_right_script,
    make_object_left_only_box_ini,
    make_object_state_script,
    make_object_timer_script,
    make_object_touch_box_ini,
    make_object_touch_script,
    make_player_control_state_script,
    make_profile,
    make_readme,
    make_runner_script,
    make_scenarios_ini,
    make_script_alias_run_script,
    make_script_return_leechcraft_npc_ini,
    make_script_return_api_script,
    make_script_sound_position_npc_ini,
    make_script_sound_position_npc_script,
    make_script_sound_position_obj_ini,
    make_script_sound_position_obj_script,
    make_script_sound_position_script,
    make_script_sound_probe_wav,
    make_script_timer_parallel_script,
    make_shop_head_numbervalid_ini,
    make_steal_script,
    make_time_stop_visual_mover_ini,
    make_time_stop_visual_repro_script,
    manual_magic_arena_magic_definitions,
    manual_magic_arena_magic_names,
    get_value,
    normalize_pack_path,
    parse_args,
    parse_dependency_ids,
    read_ini,
    resolve_contained_path,
    sanitize_save_namespace,
    scenario_files,
    scaffold,
    to_lower_ascii,
    validate_generated_pack,
)


def assert_contains(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


def make_minimal_assets(root: Path) -> Path:
    assets_root = root / "assets"
    resources_lines = [
        "[Collection]",
        "CommonPath=common",
    ]
    assets_root.mkdir(parents=True, exist_ok=True)
    (assets_root / "resources.ini").write_text(
        "\n".join([*resources_lines, ""]),
        encoding="utf-8",
    )
    (assets_root / "common").mkdir(parents=True, exist_ok=True)
    base_profile = assets_root / "xjxqy" / "game_profile.ini"
    base_profile.parent.mkdir(parents=True, exist_ok=True)
    base_profile.write_text(
        "\n".join(
            [
                "[Game]",
                "Id=XJXQY",
                "Name=Base",
                "Type=2",
                "[Combat]",
                "MinimumMagicDamage=5",
                "[Resource]",
                "TextEncodingConverted=1",
                "[Save]",
                "Namespace=XJXQY",
                "[Title]",
                "Menu=ini\\ui\\title\\title.menu.ini",
                "[NewGame]",
                "Script=newgame.txt",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return assets_root


def test_ordered_multiple_dependency_ids() -> None:
    assert parse_dependency_ids("JXQY2, YYCS, jxqy2") == ["JXQY2", "YYCS"]


def test_script_timer_parallel_alias_contract() -> None:
    script = make_script_timer_parallel_script().lower()
    for alias_call in [
        'messagebox("mod_test messagebox alias");',
        'message("mod_test message alias");',
        'assing("mod_test_script_assing_alias", 1);',
        'setvar("mod_test_script_setvar_alias", 1);',
        'showsystemmessage("mod_test system message alias", 500);',
        "enabeldrop();",
        'getgoodsmun("mod_test_goods_lifecycle_stack.ini");',
        'runscirpt("mod_test_script_alias_run.txt");',
        "setplayrdir(5);",
        "centercamera();",
    ]:
        assert_contains(script, alias_call, "script_timer_parallel alias call")

    scenarios_ini = make_scenarios_ini()
    assert_contains(scenarios_ini, "TimeoutSeconds=180", "magic_collision scenario timeout")
    for expected_variable in [
        "mod_test_script_messagebox_alias=1",
        "mod_test_script_message_alias=1",
        "mod_test_script_assing_alias=1",
        "mod_test_script_setvar_alias=1",
        "mod_test_script_system_message_alias=1",
        "mod_test_script_enabeldrop_alias=1",
        "mod_test_script_getgoodsmun_alias=2",
        "mod_test_script_runscirpt_alias=1",
        "mod_test_script_setplayrdir_alias=5",
        "mod_test_script_centercamera_alias=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "script_timer_parallel expectation")

    generated_files = scenario_files()
    alias_script_path = "script/common/mod_test_script_alias_run.txt"
    if alias_script_path not in generated_files:
        raise AssertionError(f"scenario files: missing {alias_script_path}")
    assert_contains(
        make_script_alias_run_script(),
        'assign("mod_test_script_runscirpt_alias", 1);',
        "RunScirpt alias target script",
    )


def test_goods_lifecycle_display_name_case_contract() -> None:
    script = make_goods_lifecycle_script()
    for required_call in [
        'getgoodsnum("mod_test_goods_lifecycle_stack.ini");',
        'delgoods("mod_test_goods_lifecycle_stack.ini");',
        'addgoods("mod_test_goods_lifecycle_case.ini");',
        'getgoodsnumbyname("MOD_TEST_GOODS_LIFECYCLE_CASE");',
        'assign("mod_test_goods_lifecycle_name_case_upper", getvar("GoodsNum"));',
        'getgoodsnumbyname("Mod_Test_Goods_Lifecycle_Case");',
        'assign("mod_test_goods_lifecycle_name_case_exact", getvar("GoodsNum"));',
        'delgoodbyname("MOD_TEST_GOODS_LIFECYCLE_CASE", 1);',
        'assign("mod_test_goods_lifecycle_name_case_after_delete", getvar("GoodsNum"));',
    ]:
        assert_contains(script, required_call, "goods_lifecycle display-name case script")
    for stale_file_reference in [
        'getgoodsnum("MOD_TEST_GOODS_LIFECYCLE_STACK.INI");',
        'delgoods("MOD_TEST_GOODS_LIFECYCLE_STACK.INI");',
    ]:
        if stale_file_reference in script:
            raise AssertionError(
                f"goods_lifecycle file reference is not lowercase: {stale_file_reference!r}"
            )

    case_ini = make_goods_lifecycle_case_ini()
    assert_contains(case_ini, "Name=Mod_Test_Goods_Lifecycle_Case", "goods_lifecycle case fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_goods_lifecycle_name_case_upper=0",
        "mod_test_goods_lifecycle_name_case_exact=1",
        "mod_test_goods_lifecycle_name_case_after_delete=0",
    ]:
        assert_contains(scenarios_ini, expected_variable, "goods_lifecycle display-name case expectation")

    generated_files = scenario_files()
    case_path = "ini/goods/mod_test_goods_lifecycle_case.ini"
    if case_path not in generated_files:
        raise AssertionError(f"scenario files: missing {case_path}")


def test_script_return_api_map_boundary_contract() -> None:
    script = make_script_return_api_script().lower()
    for required_call in [
        'getmapstate(0, 0, "hasmap", "mod_test_return_map_has_map");',
        'getmapstate(0, 0, "width", "mod_test_return_map_width");',
        'if getvar("mod_test_return_map_width") > 0 then assign("mod_test_return_map_width_positive", 1) end',
        'getmapstate(0, 0, "height", "mod_test_return_map_height");',
        'if getvar("mod_test_return_map_height") > 0 then assign("mod_test_return_map_height_positive", 1) end',
        'getmapstate(-1, -1, "isinmap", "mod_test_return_map_outside");',
        'getmapstate(-1, -1, "obstacle", "mod_test_return_map_outside_obstacle");',
    ]:
        assert_contains(script, required_call, "script_return_api MapState boundary contract")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_return_map_has_map=1",
        "mod_test_return_map_width_positive=1",
        "mod_test_return_map_height_positive=1",
        "mod_test_return_map_outside=0",
        "mod_test_return_map_outside_obstacle=-1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "script_return_api expectation")

    generated_files = scenario_files()
    script_path = "script/common/mod_test_script_return_api.txt"
    if script_path not in generated_files:
        raise AssertionError(f"scenario files: missing {script_path}")


def test_script_return_api_leechcraft_contract() -> None:
    script = make_script_return_api_script().lower()
    for required_call in [
        'addnpc("mod_test_script_return_leechcraft_npc.ini", 35, 21, 0);',
        'getnpcstate("mod_test_script_return_leechcraft", "leechcraft", "mod_test_return_leechcraft_required");',
        'assign("yiliao", 1);',
        'getleechcraftdifference("mod_test_script_return_leechcraft", "mod_test_return_leechcraft_short");',
        'assign("yiliao", 3);',
        'getleechcraftdifference("mod_test_script_return_leechcraft", "mod_test_return_leechcraft_success");',
        'getleechcraftdifference("mod_test_script_return_missing", "mod_test_return_leechcraft_missing");',
        'assign("yiliao", 0);',
    ]:
        assert_contains(script, required_call, "script_return_api Leechcraft contract")

    leechcraft_ini = make_script_return_leechcraft_npc_ini()
    for required_line in [
        "Name=MOD_TEST_SCRIPT_RETURN_LEECHCRAFT",
        "Leechcraft=3",
    ]:
        assert_contains(leechcraft_ini, required_line, "script_return_api Leechcraft fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_return_leechcraft_required=3",
        "mod_test_return_leechcraft_short=2",
        "mod_test_return_leechcraft_success=-1",
        "mod_test_return_leechcraft_missing=0",
    ]:
        assert_contains(scenarios_ini, expected_variable, "script_return_api Leechcraft expectation")

    generated_files = scenario_files()
    fixture_path = "ini/npc/mod_test_script_return_leechcraft_npc.ini"
    if fixture_path not in generated_files:
        raise AssertionError(f"scenario files: missing {fixture_path}")


def test_player_movement_alias_contract() -> None:
    script = make_player_control_state_script().lower()
    for alias_call in [
        "playgoto(31, 20);",
        "playerwalkto(32, 20);",
        "playerwalktodir(0, 0);",
        "playerwalktononblocking(32, 20);",
        "playerruntononblocking(32, 20);",
    ]:
        assert_contains(script, alias_call, "player_control_state movement alias call")

    scenarios_ini = make_scenarios_ini()
    assert_contains(
        scenarios_ini,
        "[Scenario.player_control_state]",
        "player_control_state scenario section",
    )
    assert_contains(
        scenarios_ini,
        "EntryScript=mod_test_player_control_state.txt",
        "player_control_state entry script",
    )
    for expected_variable in [
        "mod_test_player_playgoto_x=31",
        "mod_test_player_playgoto_y=20",
        "mod_test_player_walkto_x=32",
        "mod_test_player_walkto_y=20",
        "mod_test_player_walktodir_alias=1",
        "mod_test_player_walkto_nonblocking_alias=1",
        "mod_test_player_runto_nonblocking_alias=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "player_control_state expectation")

    generated_files = scenario_files()
    script_path = "script/common/mod_test_player_control_state.txt"
    if script_path not in generated_files:
        raise AssertionError(f"scenario files: missing {script_path}")


def test_object_script_alias_contract() -> None:
    script = make_object_state_script().lower()
    for alias_call in [
        'setobjoffset("mod_test_object_alias", 11, -7);',
        'deleteobj("mod_test_object_alias");',
        'lodaobj("mod_test_object_alias_runtime_save.obj");',
        'runobjscript("mod_test_object_alias");',
    ]:
        assert_contains(script, alias_call, "object_state alias call")

    scenarios_ini = make_scenarios_ini()
    assert_contains(
        scenarios_ini,
        "[Scenario.object_state]",
        "object_state scenario section",
    )
    assert_contains(
        scenarios_ini,
        "EntryScript=mod_test_object_state.txt",
        "object_state entry script",
    )
    for expected_variable in [
        "mod_test_object_setobjoffset_alias_x=11",
        "mod_test_object_setobjoffset_alias_y=-7",
        "mod_test_object_deleteobj_alias_exists_after=0",
        "mod_test_object_lodaobj_alias_exists_after=1",
        "mod_test_object_lodaobj_alias_offset_x=11",
        "mod_test_object_lodaobj_alias_offset_y=-7",
        "mod_test_object_deletecurrent_alias_script=1",
        "mod_test_object_deletecurrent_alias_exists_after=0",
    ]:
        assert_contains(scenarios_ini, expected_variable, "object_state expectation")

    generated_files = scenario_files()
    object_script_path = "script/common/mod_test_object_state.txt"
    if object_script_path not in generated_files:
        raise AssertionError(f"scenario files: missing {object_script_path}")
    alias_script_path = "script/common/mod_test_object_alias_delete_current.txt"
    if alias_script_path not in generated_files:
        raise AssertionError(f"scenario files: missing {alias_script_path}")
    assert_contains(
        make_object_alias_delete_current_script().lower(),
        "deletecurrentobj();",
        "DeleteCurrentObj alias target script",
    )


def test_object_timer_current_object_offset_contract() -> None:
    script = make_object_timer_script().lower()
    for required_call in [
        'getobjstate("", "exists", "mod_test_object_timer_self_exists_before");',
        "setobjofs(13, -9);",
        'getobjstate("", "offx", "mod_test_object_timer_self_offset_x");',
        'getobjstate("", "offy", "mod_test_object_timer_self_offset_y");',
    ]:
        assert_contains(script, required_call, "object timer current-object offset contract")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_object_timer_self_offset_x=13",
        "mod_test_object_timer_self_offset_y=-9",
    ]:
        assert_contains(scenarios_ini, expected_variable, "object_state current-object offset expectation")


def test_object_touch_trigger_contract() -> None:
    script = make_object_state_script().lower()
    for required_call in [
        'assign("mod_test_object_touched", 0);',
        'addobj("mod_test_object_touch_box.ini", 32, 20, 0);',
        'getobjstate("mod_test_object_touch", "hasinteractscript", "mod_test_object_touch_has_script");',
        'getobjstate("mod_test_object_touch", "scriptfilejusttouch", "mod_test_object_touch_just_touch");',
        'getobjstate("mod_test_object_touch", "canselectforinteraction", "mod_test_object_touch_can_select");',
        'addobj("mod_test_object_touch_box.ini", 36, 20, 0);',
        'assign("mod_test_interaction_chain", 1);',
        "setwalkisrun(1);",
        "disablerun();",
        'assign("mod_test_object_interact_action_queued", interactnearestobj(1, 0, 2));',
    ]:
        assert_contains(script, required_call, "object_state touch-only trigger contract")

    touch_ini = make_object_touch_box_ini()
    for required_line in [
        "ObjName=MOD_TEST_OBJECT_TOUCH",
        "CanInteractDirectly=0",
        "ScriptFileJustTouch=1",
        "ScriptFile=mod_test_object_touch.txt",
        "ScriptFileRight=0",
    ]:
        assert_contains(touch_ini, required_line, "object touch fixture")

    touch_script = make_object_touch_script().lower()
    for required_call in [
        'assign("mod_test_object_touched", 1);',
        'delobj("");',
    ]:
        assert_contains(touch_script, required_call, "object touch script")

    right_script = make_object_interact_right_script().lower()
    for required_call in [
        'assign("mod_test_object_interacted_right", 1);',
        "setwalkisrun(0);",
        "enablerun();",
    ]:
        assert_contains(right_script, required_call, "WalkIsRun fallback interaction cleanup")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_object_touch_has_script=1",
        "mod_test_object_touch_just_touch=1",
        "mod_test_object_touch_can_select=0",
        "mod_test_object_interact_action_queued=1",
        "mod_test_object_touched=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "object_state touch expectation")

    generated_files = scenario_files()
    for required_path in [
        "ini/obj/mod_test_object_touch_box.ini",
        "script/common/mod_test_object_touch.txt",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_object_right_script_missing_contract() -> None:
    script = make_object_state_script().lower()
    for required_call in [
        'delobj("mod_test_object_left_only");',
        'addobj("mod_test_object_left_only_box.ini", 36, 20, 0);',
        'if interactnearestobj(1, 0, 0) == 0 then assign("mod_test_object_interact_right_missing_blocked", 1) end',
        'delnpc("mod_test_interact_npc");',
        'addnpc("mod_test_npc_shop_death.ini", 36, 20, 0);',
        'if interactnearestnpc(1, 0, 0) == 0 then assign("mod_test_npc_interact_right_missing_blocked", 1) end',
    ]:
        assert_contains(script, required_call, "object_state right-script missing contract")

    object_ini = make_object_left_only_box_ini()
    for required_line in [
        "ObjName=MOD_TEST_OBJECT_LEFT_ONLY",
        "CanInteractDirectly=1",
        "ScriptFile=mod_test_object_interact.txt",
        "ScriptFileRight=",
    ]:
        assert_contains(object_ini, required_line, "left-only object fixture")
    if "ScriptFileRight=mod_test_object_interact_right.txt" in object_ini:
        raise AssertionError("left-only object fixture must not define a right script")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_object_interact_right_missing_blocked=1",
        "mod_test_npc_interact_right_missing_blocked=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "object_state expectation")

    generated_files = scenario_files()
    object_path = "ini/obj/mod_test_object_left_only_box.ini"
    if object_path not in generated_files:
        raise AssertionError(f"scenario files: missing {object_path}")


def test_npc_shop_death_contract() -> None:
    script = make_object_state_script().lower()
    for required_call in [
        "cleargoods();",
        "cleareffect();",
        "clearmagic();",
        'addmagic("mod_test_magic_shop_death_kill.ini");',
        'setmagiclevel("mod_test_magic_shop_death_kill.ini", 1);',
        'addnpc("mod_test_npc_shop_death.ini", 36, 17, 0);',
        'getnpcstate("mod_test_shop_death_npc", "hasbuyinifile", "mod_test_npc_shop_death_has_buy_file");',
        'getnpcstate("mod_test_shop_death_npc", "hasbuyinistring", "mod_test_npc_shop_death_has_buy_string");',
        'usemagic("mod_test_magic_shop_death_kill.ini", 36, 17);',
        "sleep(1800);",
        'getnpcstate("mod_test_shop_death_npc", "isdeath", "mod_test_npc_shop_death_dead");',
        'getgoodsnum("mod_test_goods_pricing_drug.ini");',
        'assign("mod_test_npc_shop_death_drop_count", getvar("goodsnum"));',
        'if getvar("mod_test_npc_shop_death_drop_count") == 3 then assign("mod_test_npc_shop_death_drop", 1) end',
    ]:
        assert_contains(script, required_call, "npc shop death script contract")

    shop_ini = make_shop_head_numbervalid_ini()
    for required_line in [
        "[Head]",
        "Count=2",
        "NumberValid=1",
        "BuyPercent=125",
        "RecyclePercent=75",
        "IniFile=mod_test_goods_pricing_drug.ini",
        "Number=3",
        "IniFile=mod_test_goods_pricing_equipment.ini",
        "Number=0",
    ]:
        assert_contains(shop_ini, required_line, "npc shop death shop fixture")

    npc_ini = make_npc_shop_death_ini()
    for required_line in [
        "Name=MOD_TEST_SHOP_DEATH_NPC",
        "Relation=1",
        "Life=10",
        "NoDropWhenDie=1",
        "ScriptFile=",
        "ScriptFileRight=",
        "BuyIniFile=mod_test_shop_head_numbervalid.ini",
    ]:
        assert_contains(npc_ini, required_line, "npc shop death NPC fixture")

    magic_ini = make_magic_shop_death_kill_ini()
    for required_line in [
        "Name=MOD_TEST_SHOP_DEATH_KILL",
        "MoveKind=1",
        "LifeFrame=20",
        "Effect=200",
        "Evade=200",
    ]:
        assert_contains(magic_ini, required_line, "npc shop death magic fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_line in [
        "[Scenario.object_state]",
        "Choice=13",
        "EntryScript=mod_test_object_state.txt",
        "mod_test_npc_shop_death_has_buy_file=1",
        "mod_test_npc_shop_death_has_buy_string=1",
        "mod_test_npc_shop_death_dead=1",
        "mod_test_npc_shop_death_drop=1",
        "shop-owner NPC death transfer",
    ]:
        assert_contains(scenarios_ini, expected_line, "npc shop death scenario metadata")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_object_state.txt",
        "ini/buy/mod_test_shop_head_numbervalid.ini",
        "ini/goods/mod_test_goods_pricing_drug.ini",
        "ini/npc/mod_test_npc_shop_death.ini",
        "ini/magic/mod_test_magic_shop_death_kill.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_goods_random_contract() -> None:
    goods_ini = make_goods_random_ini()
    for required_line in [
        "Name=MOD_TEST_GOODS_RANDOM",
        "Kind=1",
        "Cost=10>20",
        "SellPrice=5,9",
        "Part=Hand",
        "Attack=1>3",
        "Defend=4,8",
        "Evade=1>-5",
        "MagicName=mod_test_magic_equipment_fly.ini",
        "MagicIniWhenUse=mod_test_magic_equipment_fly.ini,mod_test_magic_equipment_fly2.ini[2]",
    ]:
        assert_contains(goods_ini, required_line, "goods random fixture")

    script = make_goods_random_script().lower()
    for required_call in [
        'displaymessage("随机物品测试开始");',
        "loadgame(0);",
        'assign("mod_test_skip_base_newgame", 1);',
        "cleargoods();",
        'addgoods("mod_test_goods_random.ini");',
        'addgoods("mod_test_goods_random.ini");',
        'getgoodsnum("mod_test_goods_random.ini");',
        'assign("mod_test_goods_random_template_count", getvar("goodsnum"));',
        'getgoodsnumbyname("mod_test_goods_random");',
        'assign("mod_test_goods_random_count", getvar("goodsnum"));',
        'if getvar("mod_test_goods_random_count") == 2 and getvar("mod_test_goods_random_template_count") == 0 then assign("mod_test_goods_random_instanced", 1) end',
        'assign("mod_test_goods_random_ready", 1);',
    ]:
        assert_contains(script, required_call, "goods random script contract")

    scenarios_ini = make_scenarios_ini()
    for expected_line in [
        "[Scenario.goods_random]",
        "Choice=10",
        "Smoke=1",
        "EntryScript=mod_test_goods_random.txt",
        "mod_test_goods_random_ready=1",
        "mod_test_goods_random_count=2",
        "mod_test_goods_random_template_count=0",
        "mod_test_goods_random_instanced=1",
        "confirms the random template file name is not stored directly",
    ]:
        assert_contains(scenarios_ini, expected_line, "goods random scenario metadata")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_goods_random.txt",
        "ini/goods/mod_test_goods_random.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_npc_drop_contract() -> None:
    script = make_npc_drop_script().lower()
    for required_call in [
        'displaymessage("npc 掉落测试开始");',
        'assign("mod_test_npc_drop_ready", 1);',
        "disablenpcai();",
        "enabledrop();",
        "setplayerpos(34,20);",
        'delobj("mod_test_drop_token");',
        'delobj("mod_test_drop_blocked_token");',
        'addnpc("mod_test_drop_npc.ini", 36, 20, 0);',
        'getnpcstate("mod_test_drop_npc", "nodropwhendie", "mod_test_npc_drop_no_drop_flag_before");',
        'setdropini("mod_test_drop_npc", "mod_test_drop_table.ini[100]");',
        'setnpcaction("mod_test_drop_npc", 11);',
        "sleep(3600);",
        'getobjstate("mod_test_drop_token", "exists", "mod_test_npc_drop_token_exists");',
        'getobjstate("mod_test_drop_token", "isdrop", "mod_test_npc_drop_token_is_drop");',
        'getobjstate("mod_test_drop_token", "mapx", "mod_test_npc_drop_token_x");',
        'getobjstate("mod_test_drop_token", "mapy", "mod_test_npc_drop_token_y");',
        'if getvar("mod_test_npc_drop_token_x") == 36 and getvar("mod_test_npc_drop_token_y") == 20 then assign("mod_test_npc_drop_token_position", 1) end',
        'addnpc("mod_test_no_drop_npc.ini", 37, 20, 0);',
        'getnpcstate("mod_test_no_drop_npc", "nodropwhendie", "mod_test_npc_drop_no_drop_flag");',
        'setnpcaction("mod_test_no_drop_npc", 11);',
        'getobjstate("mod_test_drop_blocked_token", "exists", "mod_test_npc_drop_blocked_token_exists");',
        'if getvar("mod_test_npc_drop_blocked_token_exists") == 0 then assign("mod_test_npc_drop_no_drop_blocked", 1) end',
        'addnpc("mod_test_default_drop_boss.ini", 38, 20, 0);',
        'setnpcaction("mod_test_default_drop_boss", 11);',
        'getobjstate("mod_test_default_drop_weapon", "exists", "mod_test_npc_default_drop_weapon_exists");',
        'getobjstate("mod_test_default_drop_armor", "exists", "mod_test_npc_default_drop_armor_exists");',
        'if getvar("mod_test_npc_default_drop_weapon_exists") == 1 or getvar("mod_test_npc_default_drop_armor_exists") == 1 then assign("mod_test_npc_default_drop_object_exists", 1) end',
        'runobjscript("mod_test_default_drop_weapon");',
        'runobjscript("mod_test_default_drop_armor");',
    ]:
        assert_contains(script, required_call, "npc drop script contract")

    drop_npc_ini = make_npc_drop_fixture_ini("MOD_TEST_DROP_NPC", "mod_test_drop_table.ini[100]")
    for required_line in [
        "Name=MOD_TEST_DROP_NPC",
        "Kind=1",
        "Relation=1",
        "Group=260",
        "NoAutoAttackPlayer=1",
        "StopFindingTarget=1",
        "DropIni=mod_test_drop_table.ini[100]",
        "NoDropWhenDie=0",
    ]:
        assert_contains(drop_npc_ini, required_line, "npc drop fixture")

    no_drop_npc_ini = make_npc_drop_fixture_ini(
        "MOD_TEST_NO_DROP_NPC",
        "mod_test_drop_blocked_table.ini[100]",
        1,
    )
    for required_line in [
        "Name=MOD_TEST_NO_DROP_NPC",
        "DropIni=mod_test_drop_blocked_table.ini[100]",
        "NoDropWhenDie=1",
    ]:
        assert_contains(no_drop_npc_ini, required_line, "npc no-drop fixture")

    default_boss_ini = make_npc_default_drop_boss_ini()
    for required_line in [
        "Name=MOD_TEST_DEFAULT_DROP_BOSS",
        "Relation=1",
        "Group=261",
        "ExpBonus=1",
        "Level=12",
        "DropIni=",
        "NoDropWhenDie=0",
    ]:
        assert_contains(default_boss_ini, required_line, "npc default drop boss fixture")

    token_ini = make_npc_drop_token_ini("MOD_TEST_DROP_TOKEN", "obj_get_money.ini")
    for required_line in [
        "ObjName=MOD_TEST_DROP_TOKEN",
        "ObjFile=obj_get_money.ini",
        "Kind=7",
        "CanInteractDirectly=1",
    ]:
        assert_contains(token_ini, required_line, "npc drop token fixture")

    table_ini = make_npc_drop_table_ini("mod_test_drop_token.ini")
    for required_line in [
        "Count=1",
        "ObjFile=mod_test_drop_token.ini",
        "Num=1",
        "Odds=1",
        "Group=1",
    ]:
        assert_contains(table_ini, required_line, "npc drop table fixture")

    default_weapon_script = make_default_drop_pickup_script("mod_test_npc_default_drop_weapon_script").lower()
    for required_call in [
        'assign("mod_test_npc_default_drop_weapon_script", 1);',
        'assign("mod_test_npc_default_drop_script", 1);',
        "delcurobj();",
        "return;",
    ]:
        assert_contains(default_weapon_script, required_call, "npc default drop pickup script")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.npc_drop]",
        "EntryScript=mod_test_npc_drop.txt",
    ]:
        assert_contains(scenarios_ini, required_line, "npc drop scenario entry")
    for expected_variable in [
        "mod_test_npc_drop_ready=1",
        "mod_test_npc_drop_no_drop_flag_before=0",
        "mod_test_npc_drop_token_exists=1",
        "mod_test_npc_drop_token_is_drop=1",
        "mod_test_npc_drop_token_position=1",
        "mod_test_npc_drop_no_drop_flag=1",
        "mod_test_npc_drop_blocked_token_exists=0",
        "mod_test_npc_drop_no_drop_blocked=1",
        "mod_test_npc_default_drop_object_exists=1",
        "mod_test_npc_default_drop_script=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc drop expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_npc_drop.txt",
        "ini/obj/mod_test_drop_token.ini",
        "ini/obj/mod_test_drop_blocked_token.ini",
        "ini/obj/mod_test_drop_table.ini",
        "ini/obj/mod_test_drop_blocked_table.ini",
        "ini/npc/mod_test_drop_npc.ini",
        "ini/npc/mod_test_no_drop_npc.ini",
        "ini/npc/mod_test_default_drop_boss.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_collision_lethal_freeze_contract() -> None:
    script = make_magic_collision_script().lower()
    assert_contains(
        script,
        'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "activeprojectileflyingdirectiony", "mod_test_magic_attack_all_trace_enemy_fly_y_before");\n'
        'sleep(80);\n'
        'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "activeprojectileflyingdirectionx", "mod_test_magic_attack_all_trace_enemy_fly_x_after");',
        "magic_collision AttackAll TraceEnemy post-retarget sampling window",
    )
    for required_call in [
        'addmagic("mod_test_magic_collision_lethal_freeze.ini");',
        'assign("mod_test_magic_lethal_freeze_status", 0);',
        'usemagic("mod_test_magic_collision_lethal_freeze.ini", 34, 16);',
        'getnpcstate("mod_test_collision_target", "isdeath", "mod_test_magic_lethal_freeze_death");',
        'getnpcstate("mod_test_collision_target", "isfrozened", "mod_test_magic_lethal_freeze_frozen");',
        'getnpcstate("mod_test_collision_target", "frozenmilliseconds", "mod_test_magic_lethal_freeze_ms");',
        'assign("mod_test_magic_lethal_freeze_status", 1)',
    ]:
        assert_contains(script, required_call, "magic_collision lethal freeze contract")

    magic_ini = make_magic_collision_lethal_freeze_ini()
    for required_line in [
        "Effect=120000",
        "Evade=200",
        "SpecialKind=1",
        "SpecialKindMilliSeconds=3000",
    ]:
        assert_contains(magic_ini, required_line, "magic_collision lethal freeze fixture")

    scenarios_ini = make_scenarios_ini()
    assert_contains(
        scenarios_ini,
        "mod_test_magic_lethal_freeze_status=1",
        "magic_collision lethal freeze expectation",
    )

    generated_files = scenario_files()
    lethal_magic_path = "ini/magic/mod_test_magic_collision_lethal_freeze.ini"
    if lethal_magic_path not in generated_files:
        raise AssertionError(f"scenario files: missing {lethal_magic_path}")


def test_magic_explode_runtime_contract() -> None:
    point_parent = make_magic_explode_point_parent_ini()
    for required_line in [
        "Name=MOD_TEST_EXPLODE_POINT_PARENT",
        "MoveKind=1",
        "LifeFrame=4",
        "ExplodeMagicFile=mod_test_magic_explode_child.ini",
    ]:
        assert_contains(point_parent, required_line, "magic explode point parent fixture")
    for forbidden_line in [
        "ExplodeWhenLifeFrameEnd=1",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        if forbidden_line in point_parent:
            raise AssertionError(
                f"magic explode point parent fixture: unexpected explicit lifetime override {forbidden_line}"
            )

    throw_parent = make_magic_explode_throw_parent_ini()
    for required_line in [
        "Name=MOD_TEST_EXPLODE_THROW_PARENT",
        "MoveKind=17",
        "ExplodeMagicFile=mod_test_magic_explode_throw_child.ini",
    ]:
        assert_contains(throw_parent, required_line, "magic explode throw parent fixture")

    suppressed_throw_parent = make_magic_explode_throw_suppressed_parent_ini()
    for required_line in [
        "Name=MOD_TEST_EXPLODE_THROW_SUPPRESSED_PARENT",
        "MoveKind=17",
        "ExplodeMagicFile=mod_test_magic_explode_throw_child.ini",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(
            suppressed_throw_parent,
            required_line,
            "magic explode suppressed throw parent fixture",
        )

    child = make_magic_explode_child_ini()
    for required_line in [
        "Name=MOD_TEST_EXPLODE_CHILD",
        "MoveKind=2",
        "Speed=0",
        "LifeFrame=200",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(child, required_line, "magic explode child fixture")

    throw_child = make_magic_explode_throw_child_ini()
    for required_line in [
        "Name=MOD_TEST_EXPLODE_THROW_CHILD",
        "MoveKind=2",
        "Speed=0",
        "LifeFrame=200",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(throw_child, required_line, "magic explode throw child fixture")

    script = make_magic_explode_script().lower()
    for required_call in [
        'getmapstate(34, 10, "canfly", "mod_test_magic_explode_point_tile_flyable");',
        'usemagic("mod_test_magic_explode_point_parent.ini", 34, 10);',
        'geteffectstate("mod_test_magic_explode_child.ini", "projectilecount", "mod_test_magic_explode_point_child_count");',
        'geteffectstate("mod_test_magic_explode_child.ini", "activeprojectileuserisplayer", "mod_test_magic_explode_point_child_owner");',
        'geteffectstate("mod_test_magic_explode_child.ini", "activeprojectilelauncherkind", "mod_test_magic_explode_point_child_launcher");',
        'setmagiclevel("mod_test_magic_explode_throw_parent.ini", 4);',
        'usemagic("mod_test_magic_explode_throw_suppressed_parent.ini", 34, 19);',
        'geteffectstate("mod_test_magic_explode_throw_suppressed_parent.ini", "projectilecount", "mod_test_magic_explode_throw_suppressed_parent_count");',
        'assign("mod_test_magic_explode_throw_suppressed_parent_seen", 1)',
        'assign("mod_test_magic_explode_throw_suppressed", 1)',
        'usemagic("mod_test_magic_explode_throw_parent.ini", 34, 19);',
        'geteffectstate("mod_test_magic_explode_throw_child.ini", "projectilecount", "mod_test_magic_explode_throw_child_count");',
        'if getvar("mod_test_magic_explode_throw_child_count") == 4 then assign("mod_test_magic_explode_throw_child_seen_four", 1) end',
        'assign("mod_test_magic_explode_point_once", 1)',
        'assign("mod_test_magic_explode_throw_four", 1)',
    ]:
        assert_contains(script, required_call, "magic explode runtime script")

    runner = make_runner_script().lower()
    for required_line in [
        'if getvar("mod_test_manual_menu_choice") == 33 then assign("mod_test_scenario_choice", 34); goto magicexplode end',
        "::magicexplode::",
        'runscript("mod_test_magic_explode.txt");',
    ]:
        assert_contains(runner, required_line, "magic explode runner dispatch")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_explode]",
        "Choice=34",
        "Smoke=1",
        "EntryScript=mod_test_magic_explode.txt",
        "mod_test_magic_explode_point_once=1",
        "mod_test_magic_explode_throw_suppressed_parent_seen=1",
        "mod_test_magic_explode_throw_suppressed_child_max=0",
        "mod_test_magic_explode_throw_suppressed=1",
        "mod_test_magic_explode_throw_child_max=4",
        "mod_test_magic_explode_throw_four=1",
    ]:
        assert_contains(scenarios_ini, required_line, "magic explode scenario metadata")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_explode.txt",
        "ini/magic/mod_test_magic_explode_point_parent.ini",
        "ini/magic/mod_test_magic_explode_throw_parent.ini",
        "ini/magic/mod_test_magic_explode_throw_suppressed_parent.ini",
        "ini/magic/mod_test_magic_explode_child.ini",
        "ini/magic/mod_test_magic_explode_throw_child.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_environment_weather_visual_contract() -> None:
    script = scaffold_module.make_environment_weather_script()
    for required_call in [
        'loadmap("map001_衡山.map");',
        'beginrain("rain1.ini");',
        "openwatereffect();",
        'assign("mod_test_environment_weather_ready", 1);',
    ]:
        assert_contains(script, required_call, "environment weather runtime script")
    if 'beginrain("Rain1.ini");' in script:
        raise AssertionError("environment weather rain resource reference is not lowercase")

    runner = make_runner_script().lower()
    for required_line in [
        'if getvar("mod_test_manual_menu_choice") == 34 then assign("mod_test_scenario_choice", 35); goto environmentweather end',
        "::environmentweather::",
        'runscript("mod_test_environment_weather.txt");',
    ]:
        assert_contains(runner, required_line, "environment weather runner dispatch")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.environment_weather]",
        "Choice=35",
        "Smoke=0",
        "EntryScript=mod_test_environment_weather.txt",
        "mod_test_environment_weather_ready=1",
    ]:
        assert_contains(scenarios_ini, required_line, "environment weather scenario metadata")

    if "script/common/mod_test_environment_weather.txt" not in scenario_files():
        raise AssertionError("scenario files: missing environment weather script")


def test_animation_parameters_visual_contract() -> None:
    resource_ini = scaffold_module.make_animation_parameter_resource_ini()
    for required_line in [
        "[Common]",
        "Image=asf/character/npc115_wlk.asf",
        "Shade=",
        "Sound=",
    ]:
        assert_contains(resource_ini, required_line, "animation parameter production resource")

    for direction in range(8):
        object_ini = scaffold_module.make_animation_parameter_object_ini(direction)
        for required_line in [
            f"ObjName=MOD_TEST_ANIMATION_DIRECTION_{direction}",
            "ObjFile=mod_test_animation_parameter_resource.ini",
            "Kind=0",
            f"Dir={direction}",
        ]:
            assert_contains(object_ini, required_line, "animation parameter direction object")

    script = scaffold_module.make_animation_parameters_visual_script().lower()
    assert_contains(script, "setplayerscn();", "animation parameter visual script camera anchor")
    for direction in range(8):
        assert_contains(
            script,
            f'addobj("mod_test_animation_direction_{direction}.ini", 31, 17, {direction});',
            "animation parameter visual script",
        )
        assert_contains(
            script,
            f'displaymessage("动画方向 {direction}：零帧间隔，固定地图锚点");',
            "animation parameter visual script direction label",
        )
    if script.count("sleep(1200);") != 7:
        raise AssertionError("animation parameter visual script: expected seven direction dwell intervals")
    assert_contains(
        script,
        'assign("mod_test_animation_parameters_visual_ready", 1);',
        "animation parameter visual script",
    )

    runner = make_runner_script().lower()
    for required_line in [
        'if getvar("mod_test_manual_menu_choice") == 35 then assign("mod_test_scenario_choice", 36); goto animationparametersvisual end',
        "::animationparametersvisual::",
        'runscript("mod_test_animation_parameters_visual.txt");',
    ]:
        assert_contains(runner, required_line, "animation parameter runner dispatch")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.animation_parameters_visual]",
        "Choice=36",
        "Smoke=0",
        "EntryScript=mod_test_animation_parameters_visual.txt",
        "mod_test_animation_parameters_visual_ready=1",
        "production npc115_wlk.asf with 80 frames, 8 directions, interval 0, per-frame offsets",
    ]:
        assert_contains(scenarios_ini, required_line, "animation parameter scenario metadata")

    generated_files = scenario_files()
    required_paths = [
        "script/common/mod_test_animation_parameters_visual.txt",
        "ini/objres/mod_test_animation_parameter_resource.ini",
        *[f"ini/obj/mod_test_animation_direction_{direction}.ini" for direction in range(8)],
    ]
    for required_path in required_paths:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_video_background_continuity_contract() -> None:
    script = scaffold_module.make_video_background_continuity_script().lower()
    for required_line in [
        "loadgame(0);",
        'assign("mod_test_skip_base_newgame", 1);',
        'loadmap("map001_衡山.map");',
        'addobj("mod_test_animation_direction_0.ini", 31, 17, 0);',
        'choose("后台持续播放测试：选择开始后立即切换到其他窗口，并保持失焦直到视频结束后至少 8 秒。", "开始验证", "取消", "mod_test_video_background_start_choice");',
        'if getvar("mod_test_video_background_start_choice") ~= 0 then return end',
        'assign("mod_test_video_background_ready", 1);',
        'playmovie("start.wmv", 0, 0, 0);',
        'assign("mod_test_video_background_movie_completed", 1);',
        "sleep(8000);",
        'assign("mod_test_video_background_game_completed", 1);',
    ]:
        assert_contains(script, required_line, "video background continuity script")

    runner = make_runner_script().lower()
    for required_line in [
        'if getvar("mod_test_manual_menu_choice") == 36 then assign("mod_test_scenario_choice", 37); goto videobackgroundcontinuity end',
        "::videobackgroundcontinuity::",
        'runscript("mod_test_video_background_continuity.txt");',
    ]:
        assert_contains(runner, required_line, "video background continuity runner dispatch")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.video_background_continuity]",
        "Choice=37",
        "Smoke=0",
        "EntryScript=mod_test_video_background_continuity.txt",
        "mod_test_video_background_ready=1",
        "mod_test_video_background_movie_completed=1",
        "mod_test_video_background_game_completed=1",
        "production XJXQY start.wmv",
    ]:
        assert_contains(scenarios_ini, required_line, "video background continuity metadata")

    generated_files = scenario_files()
    if "script/common/mod_test_video_background_continuity.txt" not in generated_files:
        raise AssertionError("scenario files: missing video background continuity script")
    if "script/common/mod_test_video_focus_pause.txt" in generated_files:
        raise AssertionError("scenario files: obsolete video focus pause script remains generated")


def test_magic_critical_feedback_contract() -> None:
    script = scaffold_module.make_magic_critical_feedback_script().lower()
    for required_line in [
        'assign("mod_test_magic_critical_feedback_ready", 1);',
        "setplayerlevel(1);",
        "addevade(10000);",
        'usemagic("mod_test_magic_critical_buff.ini", 34, 20);',
        'getplayerstate("critchance", "mod_test_magic_critical_chance");',
        'getplayerstate("critdamage", "mod_test_magic_critical_damage_percent");',
        "for i = 1, 6 do",
        'usemagic("mod_test_magic_critical_strike.ini", 36, 20);',
        'assign("mod_test_magic_critical_total_damage", getvar("mod_test_magic_critical_initial_life") - getvar("mod_test_magic_critical_final_life"));',
        'getvar("mod_test_magic_critical_total_damage") == 1200',
        'assign("mod_test_magic_critical_feedback_pass", 1)',
        'mg 暴击检查：目标头顶应显示橙色文字‘暴击 200’',
    ]:
        assert_contains(script, required_line, "MG critical feedback script")

    buff_ini = scaffold_module.make_magic_critical_buff_ini()
    for required_line in [
        "MoveKind=13",
        "SpecialKind=99",
        "CritChanceAddValue=100",
        "CritDamageAddPercent=99",
        "LifeFrame=3000",
    ]:
        assert_contains(buff_ini, required_line, "MG critical buff fixture")

    strike_ini = scaffold_module.make_magic_critical_strike_ini()
    for required_line in [
        "MoveKind=1",
        "Effect=100",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(strike_ini, required_line, "MG critical strike fixture")

    runner = make_runner_script().lower()
    for required_line in [
        'if getvar("mod_test_manual_menu_choice") == 37 then assign("mod_test_scenario_choice", 38); goto magiccriticalfeedback end',
        "::magiccriticalfeedback::",
        'runscript("mod_test_magic_critical_feedback.txt");',
    ]:
        assert_contains(runner, required_line, "MG critical feedback runner dispatch")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_critical_feedback]",
        "Choice=38",
        "Smoke=0",
        "EntryScript=mod_test_magic_critical_feedback.txt",
        "mod_test_magic_critical_feedback_pass=1",
        "temporary explicit RageSystem=1 profile activation",
    ]:
        assert_contains(scenarios_ini, required_line, "MG critical feedback scenario metadata")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_critical_feedback.txt",
        "ini/npc/mod_test_magic_critical_target_npc.ini",
        "ini/magic/mod_test_magic_critical_buff.ini",
        "ini/magic/mod_test_magic_critical_strike.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_detached_caster_visual_contract() -> None:
    script = scaffold_module.make_magic_detached_caster_visual_script().lower()
    for required_line in [
        'assign("mod_test_magic_detached_caster_ready", 1);',
        "setplayerpos(34,24);",
        "for cycle = 1, 6 do",
        'npcusemagic("mod_test_collision_caster", "001火药炮.ini", 36, 20, 1);',
        'geteffectstate("001火药炮.ini", "projectilecount", "mod_test_magic_detached_caster_parent_count");',
        'geteffectstate("001霹雳烟火弹爆炸.ini", "count", "mod_test_magic_detached_caster_child_before_delete");',
        'delnpc("mod_test_collision_caster");',
        'getnpcstate("mod_test_collision_caster", "exists", "mod_test_magic_detached_caster_exists_after_delete");',
        'geteffectstate("001火药炮.ini", "userkind", "mod_test_magic_detached_caster_owner_kind_after_delete");',
        'getvar("mod_test_magic_detached_caster_child_before_delete") == 0',
        'getvar("mod_test_magic_detached_caster_exists_after_delete") == 0',
        'getvar("mod_test_magic_detached_caster_owner_kind_after_delete") == 1',
        'getvar("mod_test_magic_detached_caster_child_seen_after_delete") == 1',
        'getvar("mod_test_magic_detached_caster_cycle_pass_count") == 6',
        'assign("mod_test_magic_detached_caster_visual_pass", 1)',
    ]:
        assert_contains(script, required_line, "F030 detached caster visual script")

    runner = make_runner_script().lower()
    for required_line in [
        'if getvar("mod_test_manual_menu_choice") == 38 then assign("mod_test_scenario_choice", 39); goto magicdetachedcastervisual end',
        "::magicdetachedcastervisual::",
        'runscript("mod_test_magic_detached_caster_visual.txt");',
    ]:
        assert_contains(runner, required_line, "F030 detached caster visual runner dispatch")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_detached_caster_visual]",
        "Choice=39",
        "Smoke=0",
        "EntryScript=mod_test_magic_detached_caster_visual.txt",
        "mod_test_magic_detached_caster_cycle_pass_count=6",
        "temporary byte-exact overlay of Xiaoxiangxing",
        "subjective sound remains a separate checkpoint",
    ]:
        assert_contains(scenarios_ini, required_line, "F030 detached caster visual scenario metadata")

    generated_files = scenario_files()
    if "script/common/mod_test_magic_detached_caster_visual.txt" not in generated_files:
        raise AssertionError("scenario files: missing detached caster visual script")


def test_magic_summon_maxcount_replacement_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_summon.ini");',
        'getplayerstate("summonednpccount", "mod_test_magic_summon_count_before");',
        'getmapstate(36, 20, "npccount", "mod_test_magic_summon_second_tile_before");',
        'usemagic("mod_test_magic_summon.ini", 35, 20);',
        'getnpcstate("mod_test_summon_npc", "issummonedbymagic", "mod_test_magic_summon_attached");',
        'getplayerstate("summonednpccount", "mod_test_magic_summon_count_first");',
        'getplayerstate("summonednpcscount", "mod_test_magic_summon_count_plural_first");',
        'usemagic("mod_test_magic_summon.ini", 36, 20);',
        'assign("mod_test_magic_summon_second_tile_added", 0);',
        'assign("mod_test_magic_summon_maxcount_replaced", 0);',
        "for i = 1, 20 do",
        "sleep(80);",
        'getplayerstate("summonednpccount", "mod_test_magic_summon_count_second");',
        'getmapstate(36, 20, "npccount", "mod_test_magic_summon_second_tile_after_second");',
        'if getvar("mod_test_magic_summon_second_tile_after_second") > getvar("mod_test_magic_summon_second_tile_before") then assign("mod_test_magic_summon_second_tile_added", 1) end',
        'if getvar("mod_test_magic_summon_count_second") == 1 and getvar("mod_test_magic_summon_second_tile_added") == 1 then assign("mod_test_magic_summon_maxcount_replaced", 1) end',
        "end",
        'npcusemagic("mod_test_collision_caster", "mod_test_magic_summon.ini", 37, 20, 1);',
        'getnpcstate("mod_test_collision_caster", "summonednpcscount", "mod_test_magic_summon_caster_count_plural");',
        'getnpcstate("mod_test_collision_caster", "livesummonednpcscount", "mod_test_magic_summon_caster_live_count_plural");',
    ]:
        assert_contains(script, required_call, "magic_collision summon MaxCount contract")

    magic_ini = make_magic_summon_ini()
    for required_line in [
        "Name=MOD_TEST_SUMMON_MAGIC",
        "MoveKind=22",
        "NpcFile=mod_test_summon_npc.ini",
        "MaxCount=1",
        "[Level1]",
    ]:
        assert_contains(magic_ini, required_line, "magic_collision summon fixture")

    npc_ini = make_magic_summon_npc_ini()
    for required_line in [
        "Name=MOD_TEST_SUMMON_NPC",
        "Kind=1",
        "Relation=1",
        "Life=100",
        "LifeMax=100",
    ]:
        assert_contains(npc_ini, required_line, "magic_collision summoned NPC fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_summon_count_before=0",
        "mod_test_magic_summon_count_first=1",
        "mod_test_magic_summon_count_plural_first=1",
        "mod_test_magic_summon_count_second=1",
        "mod_test_magic_summon_second_tile_added=1",
        "mod_test_magic_summon_maxcount_replaced=1",
        "summon ownership plus MaxCount replacement at a new tile",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic_collision summon MaxCount expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_collision.txt",
        "ini/magic/mod_test_magic_summon.ini",
        "ini/npc/mod_test_summon_npc.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_vibrating_screen_contract() -> None:
    script = make_magic_summon_body_script().lower()
    assert_contains(
        script,
        'getmagicstate("mod_test_magic_body_medium.ini", "vibratingscreen", "mod_test_magic_body_vibrating");',
        "magic_summon_body VibratingScreen readback",
    )

    magic_ini = make_magic_body_medium_ini()
    for required_line in [
        "BodyRadius=2",
        "VibratingScreen=6",
    ]:
        assert_contains(magic_ini, required_line, "magic body vibrating fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_summon_body]",
        "EntryScript=mod_test_magic_summon_body.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "magic_summon_body VibratingScreen scenario entry",
        )
    assert_contains(
        scenarios_ini,
        "mod_test_magic_body_vibrating=6",
        "magic_summon_body VibratingScreen expectation",
    )

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_summon_body.txt",
        "ini/magic/mod_test_magic_body_medium.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_range_attack_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_range_attack.ini");',
        'getmagicstate("mod_test_magic_range_attack.ini", "rangeeffect", "mod_test_magic_range_attack_effect");',
        'getmagicstate("mod_test_magic_range_attack.ini", "rangetimeinterval", "mod_test_magic_range_attack_interval");',
        'getmagicstate("mod_test_magic_range_attack.ini", "rangefreeze", "mod_test_magic_range_attack_freeze_ms", 1);',
        'getmagicstate("mod_test_magic_range_attack.ini", "rangedamage", "mod_test_magic_range_attack_damage_value", 1);',
        'usemagic("mod_test_magic_range_attack.ini", 34, 20);',
        'getnpcstate("mod_test_collision_target", "isfrozened", "mod_test_magic_range_attack_frozen_raw");',
        'getnpcstate("mod_test_collision_target", "life", "mod_test_magic_range_attack_life_after");',
        'assign("mod_test_magic_range_attack_no_mana_damage", 1)',
    ]:
        assert_contains(script, required_call, "magic_collision RangeAttack contract")

    magic_ini = make_magic_range_attack_ini()
    for required_line in [
        "RangeEffect=1",
        "RangeRadius=4",
        "RangeTimeInterval=100",
        "Effect2=17",
        "Effect3=13",
        "EffectMana=15",
        "RangeFreeze=900",
        "RangeDamage=25",
    ]:
        assert_contains(magic_ini, required_line, "magic RangeAttack fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_collision]",
        "EntryScript=mod_test_magic_collision.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "magic_collision RangeAttack scenario entry",
        )
    for expected_variable in [
        "mod_test_magic_range_attack=1",
        "mod_test_magic_range_attack_state=1",
        "mod_test_magic_range_attack_frozen=1",
        "mod_test_magic_range_attack_damage=1",
        "mod_test_magic_range_attack_no_mana_damage=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic RangeAttack expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_collision.txt",
        "ini/magic/mod_test_magic_range_attack.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_damage_channels_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_damage_channels.ini");',
        'addnpc("mod_test_magic_damage_channels_target_npc.ini", 34, 16, 0);',
        'getmagicstate("mod_test_magic_damage_channels.ini", "effect", "mod_test_magic_damage_channels_effect", 1);',
        'getmagicstate("mod_test_magic_damage_channels.ini", "effectext", "mod_test_magic_damage_channels_effect_ext", 1);',
        'getmagicstate("mod_test_magic_damage_channels.ini", "effect2", "mod_test_magic_damage_channels_effect2", 1);',
        'getmagicstate("mod_test_magic_damage_channels.ini", "effect3", "mod_test_magic_damage_channels_effect3", 1);',
        'getmagicstate("mod_test_magic_damage_channels.ini", "effectmana", "mod_test_magic_damage_channels_effect_mana", 1);',
        'getnpcstate("mod_test_damage_channels_target", "defend2", "mod_test_magic_damage_channels_defend2");',
        'getnpcstate("mod_test_damage_channels_target", "defend3", "mod_test_magic_damage_channels_defend3");',
        'if getvar("mod_test_magic_damage_channels_effect") == 20 and getvar("mod_test_magic_damage_channels_effect_ext") == 9 and getvar("mod_test_magic_damage_channels_effect2") == 17 and getvar("mod_test_magic_damage_channels_effect3") == 13 and getvar("mod_test_magic_damage_channels_effect_mana") == 15 and getvar("mod_test_magic_damage_channels_defend2") == 5 and getvar("mod_test_magic_damage_channels_defend3") == 3 then assign("mod_test_magic_damage_channels_fields", 1) end',
        'usemagic("mod_test_magic_damage_channels.ini", 34, 10);',
        'if getvar("mod_test_magic_damage_channels_life_after") <= getvar("mod_test_magic_damage_channels_life_before") - 42 then assign("mod_test_magic_damage_channels_life_delta", 1) end',
        'if getvar("mod_test_magic_damage_channels_life_after") <= getvar("mod_test_magic_damage_channels_life_before") - 51 then assign("mod_test_magic_damage_channels_effect_ext_delta", 1) end',
        'if getvar("mod_test_magic_damage_channels_mana_after") <= getvar("mod_test_magic_damage_channels_mana_before") - 15 then assign("mod_test_magic_damage_channels_mana_delta", 1) end',
    ]:
        assert_contains(script, required_call, "magic_collision damage channels script contract")

    magic_ini = make_magic_damage_channels_ini()
    for required_line in [
        "Name=MOD_TEST_DAMAGE_CHANNELS",
        "MoveKind=2",
        "Effect=20",
        "EffectExt=9",
        "Effect2=17",
        "Effect3=13",
        "EffectMana=15",
    ]:
        assert_contains(magic_ini, required_line, "magic damage channels fixture")

    target_ini = make_magic_damage_channels_target_npc_ini()
    for required_line in [
        "Name=MOD_TEST_DAMAGE_CHANNELS_TARGET",
        "Life=99999",
        "Mana=100",
        "Defend2=5",
        "Defend3=3",
    ]:
        assert_contains(target_ini, required_line, "magic damage channels target fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_collision]",
        "EntryScript=mod_test_magic_collision.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "magic_collision damage channels scenario entry",
        )
    for expected_variable in [
        "mod_test_magic_damage_channels_fields=1",
        "mod_test_magic_damage_channels_life_delta=1",
        "mod_test_magic_damage_channels_effect_ext_delta=1",
        "mod_test_magic_damage_channels_mana_delta=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic damage channels expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_collision.txt",
        "ini/magic/mod_test_magic_damage_channels.ini",
        "ini/npc/mod_test_magic_damage_channels_target_npc.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_leap_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_leap.ini");',
        'displaymessage("正在测试武功跳跃");',
        'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
        'addnpc("mod_test_collision_blocker_npc.ini", 34, 15, 0);',
        'getmagicstate("mod_test_magic_leap.ini", "leaptimes", "mod_test_magic_leap_times", 1);',
        'getmagicstate("mod_test_magic_leap.ini", "leapframe", "mod_test_magic_leap_frame", 1);',
        'getmagicstate("mod_test_magic_leap.ini", "effectreducepercentage", "mod_test_magic_leap_reduce_percentage", 1);',
        'if getvar("mod_test_magic_leap_times") == 1 and getvar("mod_test_magic_leap_frame") == 20 and getvar("mod_test_magic_leap_reduce_percentage") == 50 then assign("mod_test_magic_leap_fields", 1) end',
        'usemagic("mod_test_magic_leap.ini", 34, 10);',
        'if getvar("mod_test_magic_leap_target1_life_after") < getvar("mod_test_magic_leap_target1_life_before") then assign("mod_test_magic_leap_target1_damage", 1) end',
        'if getvar("mod_test_magic_leap_target1_life_after") == getvar("mod_test_magic_leap_target1_life_before") - 70 then assign("mod_test_magic_leap_target1_single_hit", 1) end',
        'if getvar("mod_test_magic_leap_target2_life_after") < getvar("mod_test_magic_leap_target2_life_before") then assign("mod_test_magic_leap_target2_damage", 1) end',
        'if getvar("mod_test_magic_leap_fields") == 1 and getvar("mod_test_magic_leap_target1_single_hit") == 1 and getvar("mod_test_magic_leap_target2_damage") == 1 then assign("mod_test_magic_leap_retarget", 1) end',
    ]:
        assert_contains(script, required_call, "magic_collision LeapTimes script contract")

    magic_ini = make_magic_leap_ini()
    for required_line in [
        "Name=MOD_TEST_LEAP",
        "MoveKind=2",
        "Effect=40",
        "Effect2=17",
        "Effect3=13",
        "EffectMana=15",
        "LeapTimes=1",
        "LeapFrame=20",
        "EffectReducePercentage=50",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic LeapTimes fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_leap=1",
        "mod_test_magic_leap_fields=1",
        "mod_test_magic_leap_target1_damage=1",
        "mod_test_magic_leap_target1_single_hit=1",
        "mod_test_magic_leap_target2_damage=1",
        "mod_test_magic_leap_retarget=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic LeapTimes expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_collision.txt",
        "ini/magic/mod_test_magic_leap.ini",
        "ini/npc/mod_test_collision_target_npc.ini",
        "ini/npc/mod_test_collision_blocker_npc.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_attack_all_leap_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_attack_all_leap.ini");',
        'displaymessage("正在测试全体攻击跳跃");',
        'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
        'addnpc("mod_test_collision_friend_npc.ini", 34, 15, 0);',
        'getmagicstate("mod_test_magic_attack_all_leap.ini", "attackall", "mod_test_magic_attack_all_leap_attack_all", 1);',
        'getmagicstate("mod_test_magic_attack_all_leap.ini", "leaptimes", "mod_test_magic_attack_all_leap_times", 1);',
        'getnpcstate("mod_test_collision_friend", "isfighter", "mod_test_magic_attack_all_leap_friend_fighter_raw");',
        'usemagic("mod_test_magic_attack_all_leap.ini", 34, 10);',
        'if getvar("mod_test_magic_attack_all_leap_target1_life_after") == getvar("mod_test_magic_attack_all_leap_target1_life_before") - 70 then assign("mod_test_magic_attack_all_leap_target1_single_hit", 1) end',
        'if getvar("mod_test_magic_attack_all_leap_friend_life_after") < getvar("mod_test_magic_attack_all_leap_friend_life_before") then assign("mod_test_magic_attack_all_leap_friendly_damage", 1) end',
        'if getvar("mod_test_magic_attack_all_leap_state") == 1 and getvar("mod_test_magic_attack_all_leap_friend_fighter") == 1 and getvar("mod_test_magic_attack_all_leap_target1_single_hit") == 1 and getvar("mod_test_magic_attack_all_leap_friendly_damage") == 1 then assign("mod_test_magic_attack_all_leap_retarget", 1) end',
        'displaymessage("正在测试伙伴全体攻击跳跃");',
        'disablepartnercombat();',
        'addnpc("mod_test_collision_partner_npc.ini", 34, 15, 0);',
        'getnpcstate("mod_test_collision_partner", "kind", "mod_test_magic_attack_all_leap_partner_kind");',
        'getnpcstate("mod_test_collision_partner", "isfighter", "mod_test_magic_attack_all_leap_partner_is_fighter");',
        'if getvar("mod_test_magic_attack_all_leap_partner_exists") == 1 and getvar("mod_test_magic_attack_all_leap_partner_kind") == 3 and getvar("mod_test_magic_attack_all_leap_partner_is_fighter") == 1 then assign("mod_test_magic_attack_all_leap_partner_ready", 1) end',
        'if getvar("mod_test_magic_attack_all_leap_partner_target1_life_after") == getvar("mod_test_magic_attack_all_leap_partner_target1_life_before") - 70 then assign("mod_test_magic_attack_all_leap_partner_target1_single_hit", 1) end',
        'if getvar("mod_test_magic_attack_all_leap_partner_life_after") < getvar("mod_test_magic_attack_all_leap_partner_life_before") then assign("mod_test_magic_attack_all_leap_partner_damage", 1) end',
        'if getvar("mod_test_magic_attack_all_leap_partner_ready") == 1 and getvar("mod_test_magic_attack_all_leap_partner_target1_single_hit") == 1 and getvar("mod_test_magic_attack_all_leap_partner_damage") == 1 then assign("mod_test_magic_attack_all_leap_partner_retarget", 1) end',
    ]:
        assert_contains(script, required_call, "magic_collision AttackAll LeapTimes script contract")

    magic_ini = make_magic_attack_all_leap_ini()
    for required_line in [
        "Name=MOD_TEST_ATTACK_ALL_LEAP",
        "AttackAll=1",
        "Effect=40",
        "Effect2=17",
        "Effect3=13",
        "EffectMana=15",
        "LeapTimes=1",
        "LeapFrame=20",
        "EffectReducePercentage=50",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic AttackAll LeapTimes fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_attack_all_leap=1",
        "mod_test_magic_attack_all_leap_state=1",
        "mod_test_magic_attack_all_leap_friend_fighter=1",
        "mod_test_magic_attack_all_leap_target1_single_hit=1",
        "mod_test_magic_attack_all_leap_friendly_damage=1",
        "mod_test_magic_attack_all_leap_retarget=1",
        "mod_test_magic_attack_all_leap_partner=1",
        "mod_test_magic_attack_all_leap_partner_ready=1",
        "mod_test_magic_attack_all_leap_partner_target1_single_hit=1",
        "mod_test_magic_attack_all_leap_partner_damage=1",
        "mod_test_magic_attack_all_leap_partner_retarget=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic AttackAll LeapTimes expectation")

    generated_files = scenario_files()
    for required_path in [
        "ini/magic/mod_test_magic_attack_all_leap.ini",
        "ini/npc/mod_test_collision_target_npc.ini",
        "ini/npc/mod_test_collision_friend_npc.ini",
        "ini/npc/mod_test_collision_partner_npc.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")

    partner_ini = make_magic_collision_partner_npc_ini()
    for required_line in [
        "Name=MOD_TEST_COLLISION_PARTNER",
        "Kind=3",
        "Relation=0",
        "Group=109",
    ]:
        assert_contains(partner_ini, required_line, "magic AttackAll LeapTimes partner fixture")


def test_magic_range_attack_all_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_range_attack_all.ini");',
        'getmagicstate("mod_test_magic_range_attack_all.ini", "attackall", "mod_test_magic_range_attack_all_attack_all", 1);',
        'getmagicstate("mod_test_magic_range_attack_all.ini", "rangefreeze", "mod_test_magic_range_attack_all_freeze_ms", 1);',
        'npcusemagic("mod_test_collision_caster", "mod_test_magic_range_attack_all.ini", 34, 18, 1);',
        'getnpcstate("mod_test_collision_caster", "isfrozened", "mod_test_magic_range_attack_all_self_frozen_raw");',
        'getnpcstate("mod_test_collision_caster", "frozenmilliseconds", "mod_test_magic_range_attack_all_self_frozen_ms_raw");',
        'disablepartnercombat();',
        'addnpc("mod_test_collision_partner_npc.ini", 34, 16, 0);',
        'getnpcstate("mod_test_collision_partner", "isfighter", "mod_test_magic_range_attack_all_partner_fighter");',
        'getnpcstate("mod_test_collision_partner", "isfrozened", "mod_test_magic_range_attack_all_partner_frozen_raw");',
        'getnpcstate("mod_test_collision_partner", "frozenmilliseconds", "mod_test_magic_range_attack_all_partner_frozen_ms_raw");',
        'assign("mod_test_magic_range_attack_all_self_status", 1)',
        'assign("mod_test_magic_range_attack_all_partner_ready", 1)',
        'assign("mod_test_magic_range_attack_all_partner_status", 1)',
        'assign("mod_test_magic_range_attack_damage", 1)',
    ]:
        assert_contains(script, required_call, "magic_collision RangeAttackAll contract")

    magic_ini = make_magic_range_attack_all_ini()
    for required_line in [
        "AttackAll=1",
        "RangeEffect=1",
        "RangeRadius=4",
        "RangeTimeInerval=100",
        "NoExplodeWhenLifeFrameEnd=1",
        "RangeFreeze=600",
    ]:
        assert_contains(magic_ini, required_line, "magic RangeAttackAll fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_range_attack_damage=1",
        "mod_test_magic_range_attack_all_partner_ready=1",
        "mod_test_magic_range_attack_all_partner_status=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic RangeAttackAll expectation")

    generated_files = scenario_files()
    magic_path = "ini/magic/mod_test_magic_range_attack_all.ini"
    if magic_path not in generated_files:
        raise AssertionError(f"scenario files: missing {magic_path}")


def test_magic_parasitic_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_parasitic.ini");',
        'usemagic("mod_test_magic_parasitic.ini", 34, 16);',
        'geteffectstate("mod_test_magic_parasitic.ini", "parasiticactivecount", "mod_test_magic_parasitic_active_count");',
        'geteffectstate("mod_test_magic_parasitic.ini", "hasparasitictarget", "mod_test_magic_parasitic_has_target");',
        'geteffectstate("mod_test_magic_parasitic.ini", "parasitictotaleffect", "mod_test_magic_parasitic_total");',
    ]:
        assert_contains(script, required_call, "magic_collision Parasitic contract")

    magic_ini = make_magic_parasitic_ini()
    for required_line in [
        "Parasitic=1",
        "ParasiticInterval=160",
        "ParasiticMaxEffect=0",
        "ParasiticMagic=mod_test_magic_collision_peer.ini",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic Parasitic fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_parasitic_active_seen=1",
        "mod_test_magic_parasitic_total_seen=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic Parasitic expectation")

    generated_files = scenario_files()
    magic_path = "ini/magic/mod_test_magic_parasitic.ini"
    if magic_path not in generated_files:
        raise AssertionError(f"scenario files: missing {magic_path}")


def test_magic_attack_all_projectile_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_attack_all_projectile.ini");',
        'delnpc("mod_test_collision_target");',
        'addnpc("mod_test_collision_friend_npc.ini", 34, 16, 0);',
        'getmagicstate("mod_test_magic_attack_all_projectile.ini", "attackall", "mod_test_magic_attack_all_projectile_attack_all", 1);',
        'getmagicstate("mod_test_magic_attack_all_projectile.ini", "effect", "mod_test_magic_attack_all_projectile_effect", 1);',
        'getnpcstate("mod_test_collision_friend", "relation", "mod_test_magic_attack_all_projectile_friend_relation");',
        'getnpcstate("mod_test_collision_friend", "isfighter", "mod_test_magic_attack_all_projectile_friend_fighter");',
        'usemagic("mod_test_magic_attack_all_projectile.ini", 34, 10);',
        'geteffectstate("mod_test_magic_attack_all_projectile.ini", "explodingcount", "mod_test_magic_attack_all_projectile_exploding_count");',
        'assign("mod_test_magic_attack_all_projectile_friendly_damage", 1)',
    ]:
        assert_contains(script, required_call, "magic_collision AttackAll projectile contract")

    magic_ini = make_magic_attack_all_projectile_ini()
    for required_line in [
        "Effect=40",
        "Speed=8",
        "MoveKind=2",
        "AttackAll=1",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic AttackAll projectile fixture")

    friend_npc_ini = make_magic_collision_friend_npc_ini()
    for required_line in [
        "Name=MOD_TEST_COLLISION_FRIEND",
        "Kind=1",
        "Relation=0",
        "Group=105",
    ]:
        assert_contains(friend_npc_ini, required_line, "magic AttackAll friend fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_collision]",
        "EntryScript=mod_test_magic_collision.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "magic_collision AttackAll projectile scenario entry",
        )
    for expected_variable in [
        "mod_test_magic_attack_all_projectile=1",
        "mod_test_magic_attack_all_projectile_state=1",
        "mod_test_magic_attack_all_projectile_friend_ready=1",
        "mod_test_magic_attack_all_projectile_exploded=1",
        "mod_test_magic_attack_all_projectile_friendly_damage=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic AttackAll projectile expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_collision.txt",
        "ini/magic/mod_test_magic_attack_all_projectile.ini",
        "ini/npc/mod_test_collision_friend_npc.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_partner_projectile_gate_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'displaymessage("正在测试伙伴弹体限制");',
        'disablepartnercombat();',
        'addnpc("mod_test_collision_caster_npc.ini", 34, 12, 0);',
        'addnpc("mod_test_collision_partner_npc.ini", 34, 16, 0);',
        'getnpcstate("mod_test_collision_partner", "isfighter", "mod_test_magic_partner_projectile_partner_fighter");',
        'npcusemagic("mod_test_collision_caster", "mod_test_magic_partner_projectile.ini", 34, 20, 1);',
        'if getvar("mod_test_magic_partner_projectile_disabled_partner_life_after") == getvar("mod_test_magic_partner_projectile_disabled_partner_life_before") then assign("mod_test_magic_partner_projectile_disabled_gate", 1) end',
        'enablepartnercombat();',
        'if getvar("mod_test_magic_partner_projectile_enabled_partner_life_after") < getvar("mod_test_magic_partner_projectile_enabled_partner_life_before") then assign("mod_test_magic_partner_projectile_enabled_hit", 1) end',
        'if getvar("mod_test_magic_partner_projectile_enabled_player_life_after") == getvar("mod_test_magic_partner_projectile_enabled_player_life_before") then assign("mod_test_magic_partner_projectile_enabled_stopped", 1) end',
        'assign("mod_test_magic_partner_projectile_gate", 1)',
    ]:
        assert_contains(script, required_call, "magic_collision ordinary projectile partner gate")

    magic_ini = make_magic_partner_projectile_ini()
    for required_line in [
        "Speed=3",
        "MoveKind=2",
        "LifeFrame=80",
        "Effect=80",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic ordinary projectile fixture")
    if "AttackAll=" in magic_ini:
        raise AssertionError("magic ordinary projectile fixture: unexpectedly defines AttackAll")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_partner_projectile_gate=1",
        "mod_test_magic_partner_projectile_ready=1",
        "mod_test_magic_partner_projectile_disabled_gate=1",
        "mod_test_magic_partner_projectile_enabled_hit=1",
        "mod_test_magic_partner_projectile_enabled_stopped=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic ordinary projectile partner gate expectation")

    generated_files = scenario_files()
    for required_path in [
        "ini/magic/mod_test_magic_partner_projectile.ini",
        "ini/npc/mod_test_collision_caster_npc.ini",
        "ini/npc/mod_test_collision_partner_npc.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_attack_all_trace_enemy_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_attack_all_trace_enemy.ini");',
        'displaymessage("正在测试全体攻击追踪敌人");',
        'addnpc("mod_test_magic_trace_nonfighter_npc.ini", 38, 16, 0);',
        'disablepartnercombat();',
        'addnpc("mod_test_collision_partner_npc.ini", 34, 34, 0);',
        'getmagicstate("mod_test_magic_attack_all_trace_enemy.ini", "attackall", "mod_test_magic_attack_all_trace_enemy_attack_all", 1);',
        'getnpcstate("mod_test_trace_nonfighter", "isfighter", "mod_test_magic_attack_all_trace_enemy_nonfighter_is_fighter");',
        'getnpcstate("mod_test_collision_partner", "kind", "mod_test_magic_attack_all_trace_enemy_partner_kind");',
        'getnpcstate("mod_test_collision_partner", "isfighter", "mod_test_magic_attack_all_trace_enemy_partner_is_fighter");',
        'if getvar("mod_test_magic_attack_all_trace_enemy_partner_exists") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_partner_kind") == 3 and getvar("mod_test_magic_attack_all_trace_enemy_partner_is_fighter") == 1 then assign("mod_test_magic_attack_all_trace_enemy_partner_ready", 1) end',
        'usemagic("mod_test_magic_attack_all_trace_enemy.ini", 42, 20);',
        'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "activeprojectileflyingdirectionx", "mod_test_magic_attack_all_trace_enemy_fly_x_before");',
        'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "activeprojectileflyingdirectiony", "mod_test_magic_attack_all_trace_enemy_fly_y_before");',
        'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "activeprojectileflyingdirectionx", "mod_test_magic_attack_all_trace_enemy_fly_x_after");',
        'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "activeprojectileflyingdirectiony", "mod_test_magic_attack_all_trace_enemy_fly_y_after");',
        'if getvar("mod_test_magic_attack_all_trace_enemy_state") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_nonfighter_ready") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_partner_ready") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_projectile_count") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_fly_y_after") > getvar("mod_test_magic_attack_all_trace_enemy_fly_y_before") and getvar("mod_test_magic_attack_all_trace_enemy_fly_x_after") < getvar("mod_test_magic_attack_all_trace_enemy_fly_x_before") then assign("mod_test_magic_attack_all_trace_enemy_fighter_trace", 1) end',
    ]:
        assert_contains(script, required_call, "magic_collision AttackAll TraceEnemy script contract")

    magic_ini = make_magic_attack_all_trace_enemy_ini()
    for required_line in [
        "Name=MOD_TEST_ATTACK_ALL_TRACE_ENEMY",
        "AttackAll=1",
        "TraceEnemy=1",
        "TraceSpeed=12",
        "TraceEnemyDelayMilliseconds=100",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic AttackAll TraceEnemy fixture")

    nonfighter_ini = make_magic_trace_nonfighter_npc_ini()
    for required_line in [
        "Name=MOD_TEST_TRACE_NONFIGHTER",
        "Kind=0",
        "Relation=0",
        "Group=108",
    ]:
        assert_contains(nonfighter_ini, required_line, "magic AttackAll TraceEnemy nonfighter fixture")

    partner_ini = make_magic_collision_partner_npc_ini()
    for required_line in [
        "Name=MOD_TEST_COLLISION_PARTNER",
        "Kind=3",
        "Relation=0",
        "Group=109",
    ]:
        assert_contains(partner_ini, required_line, "magic AttackAll TraceEnemy partner fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "ExpectVariablesExtra=",
        "mod_test_magic_attack_all_trace_enemy=1",
        "mod_test_magic_attack_all_trace_enemy_state=1",
        "mod_test_magic_attack_all_trace_enemy_nonfighter_ready=1",
        "mod_test_magic_attack_all_trace_enemy_partner_ready=1",
        "mod_test_magic_attack_all_trace_enemy_fighter_trace=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic AttackAll TraceEnemy expectation")

    generated_files = scenario_files()
    for required_path in [
        "ini/magic/mod_test_magic_attack_all_trace_enemy.ini",
        "ini/npc/mod_test_magic_trace_nonfighter_npc.ini",
        "ini/npc/mod_test_collision_partner_npc.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_carry_user_hidden_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_carry_user4.ini");',
        'addmagic("mod_test_magic_carry_user4_hidden.ini");',
        'addmagic("mod_test_magic_carry_user1_hidden.ini");',
        'displaymessage("正在测试隐藏一号施法者携带");',
        'assign("mod_test_magic_carry_user4_hide_exploding_hidden_seen", 0);',
        'geteffectstate("mod_test_magic_carry_user4_hidden.ini", "explodingcount", "mod_test_magic_carry_user4_hide_exploding");',
        'if getvar("mod_test_magic_carry_user4_hide_exploding") > 0 and getvar("mod_test_magic_carry_user4_hide_draw") == 0 then assign("mod_test_magic_carry_user4_hide_exploding_hidden_seen", 1) end',
        'assign("mod_test_magic_carry_user1_hide_hidden_seen", 0);',
        'assign("mod_test_magic_carry_user1_hide_exploding_hidden_seen", 0);',
        'assign("mod_test_magic_carry_user1_hide_player_moved", 0);',
        'assign("mod_test_magic_carry_user1_hide_restored", 0);',
        'usemagic("mod_test_magic_carry_user1_hidden.ini", 34, 16);',
        'geteffectstate("mod_test_magic_carry_user1_hidden.ini", "carryuseractive", "mod_test_magic_carry_user1_hide_active");',
        'geteffectstate("mod_test_magic_carry_user1_hidden.ini", "explodingcount", "mod_test_magic_carry_user1_hide_exploding");',
        'getplayerstate("isdraw", "mod_test_magic_carry_user1_hide_draw");',
        'getplayerstate("mapy", "mod_test_magic_carry_user1_hide_player_y");',
        'getplayerstate("hasnonzerooffset", "mod_test_magic_carry_user1_hide_player_offset");',
        'if getvar("mod_test_magic_carry_user1_hide_active") == 1 and getvar("mod_test_magic_carry_user1_hide_draw") == 0 then assign("mod_test_magic_carry_user1_hide_hidden_seen", 1) end',
        'if getvar("mod_test_magic_carry_user1_hide_exploding") > 0 and getvar("mod_test_magic_carry_user1_hide_draw") == 0 then assign("mod_test_magic_carry_user1_hide_exploding_hidden_seen", 1) end',
        'if getvar("mod_test_magic_carry_user1_hide_active") == 1 and getvar("mod_test_magic_carry_user1_hide_player_y") < 20 then assign("mod_test_magic_carry_user1_hide_player_moved", 1) end',
        'if getvar("mod_test_magic_carry_user1_hide_active") == 1 and getvar("mod_test_magic_carry_user1_hide_player_offset") == 1 then assign("mod_test_magic_carry_user1_hide_player_moved", 1) end',
        'if getvar("mod_test_magic_carry_user1_hide_hidden_seen") == 1 and getvar("mod_test_magic_carry_user1_hide_active") == 0 and getvar("mod_test_magic_carry_user1_hide_draw") == 1 then assign("mod_test_magic_carry_user1_hide_restored", 1) end',
        'assign("mod_test_magic_carry_user_hidden_contract", 1);',
        'if getvar("mod_test_magic_carry_user4_hide_hidden_seen") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
        'if getvar("mod_test_magic_carry_user4_hide_restored") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
        'if getvar("mod_test_magic_carry_user4_hide_exploding_hidden_seen") ~= 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
        'if getvar("mod_test_magic_carry_user1_hide_hidden_seen") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
        'if getvar("mod_test_magic_carry_user1_hide_exploding_hidden_seen") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
        'if getvar("mod_test_magic_carry_user1_hide_player_moved") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
        'if getvar("mod_test_magic_carry_user1_hide_restored") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
    ]:
        assert_contains(script, required_call, "magic_collision CarryUser hidden contract")

    carry_user4_ini = make_magic_carry_user4_hidden_ini()
    for required_line in [
        "Name=MOD_TEST_CARRY_USER4_HIDDEN",
        "CarryUser=4",
        "CarryUserSpriteIndex=0",
        "HideUserWhenCarry=1",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(carry_user4_ini, required_line, "magic CarryUser=4 hidden fixture")

    carry_user1_ini = make_magic_carry_user1_hidden_ini()
    for required_line in [
        "Name=MOD_TEST_CARRY_USER1_HIDDEN",
        "CarryUser=1",
        "CarryUserSpriteIndex=0",
        "HideUserWhenCarry=1",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(carry_user1_ini, required_line, "magic CarryUser=1 hidden fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_carry_user4_hide_hidden_seen=1",
        "mod_test_magic_carry_user4_hide_exploding_hidden_seen=0",
        "mod_test_magic_carry_user4_hide_restored=1",
        "mod_test_magic_carry_user1_hide_hidden_seen=1",
        "mod_test_magic_carry_user1_hide_exploding_hidden_seen=1",
        "mod_test_magic_carry_user1_hide_player_moved=1",
        "mod_test_magic_carry_user1_hide_restored=1",
        "mod_test_magic_carry_user_hidden_contract=1",
    ]:
        assert_contains(
            scenarios_ini,
            expected_variable,
            "magic CarryUser hidden expectation",
        )
    for required_line in [
        "[Scenario.magic_collision]",
        "EntryScript=mod_test_magic_collision.txt",
    ]:
        assert_contains(scenarios_ini, required_line, "magic CarryUser hidden scenario entry")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_collision.txt",
        "ini/magic/mod_test_magic_carry_user4_hidden.ini",
        "ini/magic/mod_test_magic_carry_user1_hidden.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_forced_handoff_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_bounce_handoff.ini");',
        'addmagic("mod_test_magic_bouncefly_handoff.ini");',
        'displaymessage("正在测试强制移动交接");',
        'assign("mod_test_magic_forced_handoff", 1);',
        'assign("mod_test_magic_forced_handoff_bounce_seen", 0);',
        'assign("mod_test_magic_forced_handoff_forced_seen", 0);',
        'assign("mod_test_magic_forced_handoff_overlap", 0);',
        'assign("mod_test_magic_forced_handoff_action_forced", 0);',
        'assign("mod_test_magic_forced_handoff_action_bounce", 0);',
        'assign("mod_test_magic_forced_handoff_cast_forced", 0);',
        'usemagic("mod_test_magic_bounce_handoff.ini", 34, 16);',
        'getnpcstate("mod_test_collision_target", "isbouncing", "mod_test_magic_forced_handoff_bounce_raw");',
        'getnpcstate("mod_test_collision_target", "ismagicforcedmoving", "mod_test_magic_forced_handoff_forced_raw");',
        'getnpcstate("mod_test_collision_target", "currentaction", "mod_test_magic_forced_handoff_action_raw");',
        'if getvar("mod_test_magic_forced_handoff_bounce_raw") == 1 then assign("mod_test_magic_forced_handoff_bounce_seen", 1) end',
        'if getvar("mod_test_magic_forced_handoff_bounce_raw") == 1 and getvar("mod_test_magic_forced_handoff_cast_forced") == 0 then usemagic("mod_test_magic_bouncefly_handoff.ini", 34, 16); assign("mod_test_magic_forced_handoff_cast_forced", 1) end',
        'if getvar("mod_test_magic_forced_handoff_forced_raw") == 1 then assign("mod_test_magic_forced_handoff_forced_seen", 1) end',
        'if getvar("mod_test_magic_forced_handoff_forced_raw") == 1 and getvar("mod_test_magic_forced_handoff_action_raw") == 26 then assign("mod_test_magic_forced_handoff_action_forced", 1) end',
    ]:
        assert_contains(script, required_call, "magic_collision forced-handoff contract")

    bounce_ini = make_magic_bounce_handoff_ini()
    for required_line in [
        "Name=MOD_TEST_BOUNCE_HANDOFF",
        "Bounce=420",
        "BounceHurt=0",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(bounce_ini, required_line, "magic Bounce handoff fixture")

    bouncefly_ini = make_magic_bouncefly_handoff_ini()
    for required_line in [
        "Name=MOD_TEST_BOUNCEFLY_HANDOFF",
        "BounceFly=1",
        "BounceFlySpeed=16",
        "BounceFlyEndHurt=0",
        "BounceFlyTouchHurt=0",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(bouncefly_ini, required_line, "magic BounceFly handoff fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_forced_handoff=1",
        "mod_test_magic_forced_handoff_bounce_seen=1",
        "mod_test_magic_forced_handoff_cast_forced=1",
        "mod_test_magic_forced_handoff_forced_seen=1",
        "mod_test_magic_forced_handoff_action_forced=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic forced-handoff expectation")
    for required_line in [
        "[Scenario.magic_collision]",
        "EntryScript=mod_test_magic_collision.txt",
    ]:
        assert_contains(scenarios_ini, required_line, "magic forced-handoff scenario entry")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_collision.txt",
        "ini/magic/mod_test_magic_bounce_handoff.ini",
        "ini/magic/mod_test_magic_bouncefly_handoff.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_pass_through_wall_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_pass_through_wall.ini");',
        'assign("mod_test_magic_wall_blocked_on_wall", getvar("mod_test_magic_wall_exploded_on_wall"));',
        'getmagicstate("mod_test_magic_pass_through_wall.ini", "passthroughwall", "mod_test_magic_pass_through_wall_flag");',
        'usemagic("mod_test_magic_pass_through_wall.ini", 27, 20);',
        'geteffectstate("mod_test_magic_pass_through_wall.ini", "projectilecount", "mod_test_magic_pass_through_wall_projectile_count");',
        'geteffectstate("mod_test_magic_pass_through_wall.ini", "activeprojectilemapx", "mod_test_magic_pass_through_wall_projectile_x");',
        'geteffectstate("mod_test_magic_pass_through_wall.ini", "explodingcount", "mod_test_magic_pass_through_wall_exploding_count");',
        'assign("mod_test_magic_pass_through_wall_crossed", 1)',
        'assign("mod_test_magic_pass_through_wall_no_explode", 1)',
        'assign("mod_test_magic_wall_exploded_on_wall", 1)',
    ]:
        assert_contains(script, required_call, "magic_collision PassThroughWall contract")

    magic_ini = make_magic_pass_through_wall_ini()
    for required_line in [
        "PassThroughWall=1",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic PassThroughWall fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_collision]",
        "EntryScript=mod_test_magic_collision.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "magic_collision PassThroughWall scenario entry",
        )
    assert_contains(
        scenarios_ini,
        "mod_test_magic_wall_exploded_on_wall=1",
        "magic PassThroughWall combined expectation",
    )

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_collision.txt",
        "ini/magic/mod_test_magic_pass_through_wall.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_post_cast_child_chain_contract() -> None:
    script = make_magic_post_cast_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_post_cast_parent.ini");',
        "addevade(200);",
        'usemagic("mod_test_magic_post_cast_parent.ini", 36, 20);',
        'getplayerstate("magicforcedmovehasendmagic", "mod_test_magic_post_cast_jump_has_end_magic");',
        'getplayerstate("magicforcedmoveprogresspermille", "mod_test_magic_post_cast_jump_progress_permille");',
        'getnpcstate("mod_test_collision_target", "life", "mod_test_magic_post_cast_life_mid");',
        'assign("mod_test_magic_post_cast_damage_mid", getvar("mod_test_magic_post_cast_life_before") - getvar("mod_test_magic_post_cast_life_mid"));',
        'getnpcstate("mod_test_collision_target", "life", "mod_test_magic_post_cast_life_after");',
        'assign("mod_test_magic_post_cast_damage_delta", getvar("mod_test_magic_post_cast_life_before") - getvar("mod_test_magic_post_cast_life_after"));',
        'if getvar("mod_test_magic_post_cast_damage_delta") >= 110 then assign("mod_test_magic_post_cast_child_chain", 1) end',
        'if getvar("mod_test_magic_post_cast_damage_delta") <= 130 then assign("mod_test_magic_post_cast_no_primary_extra", 1) end',
    ]:
        assert_contains(script, required_call, "magic_post_cast child-chain contract")

    parent_ini = make_magic_post_cast_parent_ini()
    for required_line in [
        "Effect=20",
        "SecondMagicFile=mod_test_magic_post_cast_second.ini",
        "SecondMagicDelay=120",
        "RandMagicFile=mod_test_magic_post_cast_rand.ini",
        "RandMagicProbability=100",
        "SideEffectPercent=100",
        "SideEffectProbability=100",
        "JumpToTarget=1",
        "JumpEndMagic=mod_test_magic_post_cast_jump_end.ini",
    ]:
        assert_contains(parent_ini, required_line, "magic_post_cast parent fixture")

    for fixture, required_line, label in [
        (make_magic_post_cast_second_ini(), "Effect=40", "magic_post_cast second fixture"),
        (make_magic_post_cast_rand_ini(), "Effect=30", "magic_post_cast rand fixture"),
        (make_magic_post_cast_jump_end_ini(), "Effect=50", "magic_post_cast jump-end fixture"),
        (make_magic_post_cast_die_ini(), "DieAfterUse=1", "magic_post_cast die fixture"),
    ]:
        assert_contains(fixture, required_line, label)

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_post_cast]",
        "EntryScript=mod_test_magic_post_cast.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "magic_post_cast scenario entry",
        )
    for expected_variable in [
        "mod_test_magic_post_cast_damage_mid=70",
        "mod_test_magic_post_cast_damage_delta=120",
        "mod_test_magic_post_cast_child_chain=1",
        "mod_test_magic_post_cast_no_primary_extra=1",
        "mod_test_magic_post_cast_die_after_use=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic_post_cast expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_post_cast.txt",
        "ini/magic/mod_test_magic_post_cast_parent.ini",
        "ini/magic/mod_test_magic_post_cast_second.ini",
        "ini/magic/mod_test_magic_post_cast_rand.ini",
        "ini/magic/mod_test_magic_post_cast_jump_end.ini",
        "ini/magic/mod_test_magic_post_cast_die.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_pass_through_contract() -> None:
    script = make_magic_collision_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_pass_through.ini");',
        'getmagicstate("mod_test_magic_pass_through.ini", "passthrough", "mod_test_magic_pass_through_flag");',
        'getmagicstate("mod_test_magic_pass_through.ini", "passthroughwithdestroyeffect", "mod_test_magic_pass_through_destroy_flag");',
        'getmagicstate("mod_test_magic_pass_through.ini", "passthroughwall", "mod_test_magic_pass_through_wall_flag");',
        'usemagic("mod_test_magic_pass_through.ini", 34, 10);',
        'geteffectstate("mod_test_magic_pass_through.ini", "activeprojectilepassthroughhitcount", "mod_test_magic_pass_through_hit_count");',
        'geteffectstate("mod_test_magic_pass_through.ini", "explodingcount", "mod_test_magic_pass_through_exploding_count");',
        'assign("mod_test_magic_pass_through_hits", 1)',
        'assign("mod_test_magic_pass_through_destroy_effect", 1)',
    ]:
        assert_contains(script, required_call, "magic_collision PassThrough contract")

    magic_ini = make_magic_pass_through_ini()
    for required_line in [
        "PassThrough=1",
        "PassThroughWithDestroyEffect=1",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic PassThrough fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_pass_through=1",
        "mod_test_magic_pass_through_state=1",
        "mod_test_magic_pass_through_target1_damage=1",
        "mod_test_magic_pass_through_target2_damage=1",
        "mod_test_magic_pass_through_hits=1",
        "mod_test_magic_pass_through_destroy_effect=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic PassThrough expectation")

    generated_files = scenario_files()
    magic_path = "ini/magic/mod_test_magic_pass_through.ini"
    if magic_path not in generated_files:
        raise AssertionError(f"scenario files: missing {magic_path}")


def test_equipment_magic_additional_effect_contract() -> None:
    script = make_equipment_trigger_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_equipment_additional_freeze.ini");',
        'getmagicstate("mod_test_magic_equipment_additional_freeze.ini", "additionaleffect", "mod_test_equipment_magic_additional_effect_state");',
        'npcusemagic("mod_test_collision_caster", "mod_test_magic_equipment_additional_freeze.ini", 34, 20, 1);',
        'getplayerstate("isfrozened", "mod_test_equipment_additional_frozen");',
        'getplayerstate("frozenmilliseconds", "mod_test_equipment_additional_frozen_ms");',
        'assign("mod_test_equipment_additional_status", 0);',
    ]:
        assert_contains(script, required_call, "equipment_trigger Magic.AdditionalEffect contract")

    magic_ini = make_magic_equipment_additional_freeze_ini()
    for required_line in [
        "Effect=0",
        "AdditionalEffect=1",
    ]:
        assert_contains(magic_ini, required_line, "equipment AdditionalEffect fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.equipment_trigger]",
        "EntryScript=mod_test_equipment_trigger.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "equipment_trigger AdditionalEffect scenario entry",
        )
    for expected_variable in [
        "mod_test_equipment_magic_additional_effect_state=1",
        "mod_test_equipment_additional_status=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "equipment AdditionalEffect expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_equipment_trigger.txt",
        "ini/magic/mod_test_magic_equipment_additional_freeze.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_npc_ai_none_fighter_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'addnpc("mod_test_npc_ai_true_neutral_target.ini", 35, 20, 0);',
        'assign("mod_test_npc_ai_friend_ignored_true_neutral", 0);',
        'addnpc("mod_test_npc_ai_friend_none_target.ini", 35, 20, 0);',
        'getnpcstate("mod_test_npc_ai_friend_none_target", "relation", "mod_test_npc_ai_friend_none_relation");',
        'getnpcstate("mod_test_npc_ai_friend_none_target", "isnonefighter", "mod_test_npc_ai_friend_none_is_none_fighter");',
        'assign("mod_test_npc_ai_friend_hit_none_fighter", 1)',
    ]:
        assert_contains(script, required_call, "npc_ai None fighter contract")

    none_fighter_ini = make_npc_ai_friend_none_target_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_AI_FRIEND_NONE_TARGET",
        "Kind=1",
        "Relation=3",
        "VisionRadius=0",
    ]:
        assert_contains(none_fighter_ini, required_line, "npc_ai None fighter fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_npc_ai_friend_neutral_has_target=0",
        "mod_test_npc_ai_friend_ignored_true_neutral=1",
        "mod_test_npc_ai_friend_none_relation=3",
        "mod_test_npc_ai_friend_none_is_none_fighter=1",
        "mod_test_npc_ai_friend_hit_none_fighter=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc_ai None fighter expectation")

    generated_files = scenario_files()
    none_fighter_path = "ini/npc/mod_test_npc_ai_friend_none_target.ini"
    if none_fighter_path not in generated_files:
        raise AssertionError(f"scenario files: missing {none_fighter_path}")


def test_npc_action_stand1_alias_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'setnpcaction("mod_test_npc_mover", 1);',
        'getnpcstate("mod_test_npc_mover", "isstanding", "mod_test_npc_ai_action1_canonical_standing");',
        'npcaction("mod_test_npc_mover", 1);',
        'getnpcstate("mod_test_npc_mover", "isstanding", "mod_test_npc_ai_action1_alias_standing");',
    ]:
        assert_contains(script, required_call, "SetNpcAction/NpcAction Stand1 script contract")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_npc_ai_action1_canonical_started=1",
        "mod_test_npc_ai_action1_canonical_standing=1",
        "mod_test_npc_ai_action1_alias_started=1",
        "mod_test_npc_ai_action1_alias_standing=1",
        "SetNpcAction/NpcAction Stand1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "SetNpcAction/NpcAction Stand1 expectation")


def test_npc_ai_none_flyer_occupancy_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'addnpc("mod_test_npc_ai_none_fighter.ini", 31, 20, 0);',
        'getnpcstate("mod_test_npc_none_fighter", "isobstacle", "mod_test_npc_ai_none_is_obstacle");',
        'getmapstate(31, 20, "canwalk", "mod_test_npc_ai_none_tile_can_walk");',
        'addnpc("mod_test_npc_ai_flyer.ini", 32, 20, 0);',
        'getnpcstate("mod_test_npc_flyer", "isobstacle", "mod_test_npc_ai_flyer_is_obstacle");',
        'getmapstate(32, 20, "canwalk", "mod_test_npc_ai_flyer_tile_can_walk");',
        'getnpcstate("mod_test_npc_flyer", "pathtype", "mod_test_npc_ai_flyer_path_type");',
        'npcgotoex("mod_test_npc_flyer", 34, 20);',
    ]:
        assert_contains(script, required_call, "npc_ai None/Flyer occupancy script contract")

    none_fighter_ini = make_npc_ai_none_fighter_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_NONE_FIGHTER",
        "Kind=1",
        "Relation=3",
    ]:
        assert_contains(none_fighter_ini, required_line, "npc_ai None occupancy fixture")

    flyer_ini = make_npc_ai_flyer_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_FLYER",
        "Kind=7",
        "Relation=2",
    ]:
        assert_contains(flyer_ini, required_line, "npc_ai Flyer occupancy fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.npc_ai]",
        "EntryScript=mod_test_npc_ai.txt",
    ]:
        assert_contains(scenarios_ini, required_line, "npc_ai None/Flyer scenario entry")
    for expected_variable in [
        "mod_test_npc_ai_none_is_obstacle=1",
        "mod_test_npc_ai_none_tile_can_walk=0",
        "mod_test_npc_ai_flyer_kind=7",
        "mod_test_npc_ai_flyer_relation=2",
        "mod_test_npc_ai_flyer_is_obstacle=0",
        "mod_test_npc_ai_flyer_is_fighter=0",
        "mod_test_npc_ai_flyer_is_interactive=0",
        "mod_test_npc_ai_flyer_tile_can_walk=1",
        "mod_test_npc_ai_flyer_path_type=4",
        "mod_test_npc_ai_flyer_use_pathfinder=1",
        "mod_test_npc_ai_flyer_step_list_len=2",
        "mod_test_npc_ai_flyer_is_walking=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc_ai None/Flyer occupancy expectation")

    generated_files = scenario_files()
    for fixture_path in [
        "script/common/mod_test_npc_ai.txt",
        "ini/npc/mod_test_npc_ai_none_fighter.ini",
        "ini/npc/mod_test_npc_ai_flyer.ini",
    ]:
        if fixture_path not in generated_files:
            raise AssertionError(f"scenario files: missing {fixture_path}")


def test_npc_ai_noadd_body_death_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'displaymessage("正在测试死亡后不生成尸体");',
        'delnpc("mod_test_npc_ai_noadd_body");',
        'delobj("mod_test_npc_ai_body");',
        'addnpc("mod_test_npc_ai_noadd_body.ini", 37, 20, 0);',
        'getnpcstate("mod_test_npc_ai_noadd_body", "isbodyiniok", "mod_test_npc_ai_noadd_body_ini_ok");',
        'getnpcstate("mod_test_npc_ai_noadd_body", "shouldaddbody", "mod_test_npc_ai_noadd_should_before");',
        'addnpcproperty("mod_test_npc_ai_noadd_body", "frozenmilliseconds", 2000);',
        'getnpcstate("mod_test_npc_ai_noadd_body", "frozenmilliseconds", "mod_test_npc_ai_noadd_frozen_ms");',
        'assign("mod_test_npc_ai_noadd_frozen_positive", 0);',
        'if getvar("mod_test_npc_ai_noadd_frozen_ms") > 0 then assign("mod_test_npc_ai_noadd_frozen_positive", 1) end',
        'setnpcaction("mod_test_npc_ai_noadd_body", 11);',
        'getnpcstate("mod_test_npc_ai_noadd_body", "isnodaddbody", "mod_test_npc_ai_noadd_body_flag");',
        'getnpcstate("mod_test_npc_ai_noadd_body", "noaddbody", "mod_test_npc_ai_noadd_alias_flag");',
        'getnpcstate("mod_test_npc_ai_noadd_body", "shouldaddbody", "mod_test_npc_ai_noadd_should_after");',
        "sleep(1200);",
        'getobjstate("mod_test_npc_ai_body", "exists", "mod_test_npc_ai_noadd_body_exists");',
    ]:
        assert_contains(script, required_call, "npc_ai no-add-body death script contract")

    noadd_ini = make_npc_ai_noadd_body_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_AI_NOADD_BODY",
        "Kind=1",
        "Relation=3",
        "Group=218",
        "BodyIni=mod_test_npc_ai_body.ini",
        "VisionRadius=0",
        "Life=100",
        "LifeMax=100",
    ]:
        assert_contains(noadd_ini, required_line, "npc_ai no-add-body fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_npc_ai_noadd_body_ini_ok=1",
        "mod_test_npc_ai_noadd_should_before=1",
        "mod_test_npc_ai_noadd_frozen_positive=1",
        "mod_test_npc_ai_noadd_body_flag=1",
        "mod_test_npc_ai_noadd_alias_flag=1",
        "mod_test_npc_ai_noadd_should_after=0",
        "mod_test_npc_ai_noadd_body_exists=0",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc_ai no-add-body expectation")

    generated_files = scenario_files()
    for required_path in [
        "ini/npc/mod_test_npc_ai_noadd_body.ini",
        "ini/obj/mod_test_npc_ai_body.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_npc_ai_path_type_maxtry_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'addnpc("mod_test_npc_ai_path_normal.ini", 31, 20, 0);',
        'addnpc("mod_test_npc_ai_path_event.ini", 32, 20, 0);',
        'addnpc("mod_test_npc_ai_path_enemy.ini", 33, 20, 0);',
        'addnpc("mod_test_npc_ai_path_best.ini", 34, 20, 0);',
        'addnpc("mod_test_npc_ai_path_fixed.ini", 35, 20, 0);',
        'getnpcstate("mod_test_npc_path_normal", "pathtype", "mod_test_npc_ai_path_normal_type");',
        'getnpcstate("mod_test_npc_path_event", "pathtype", "mod_test_npc_ai_path_event_type");',
        'getnpcstate("mod_test_npc_path_enemy", "pathtype", "mod_test_npc_ai_path_enemy_type");',
        'getnpcstate("mod_test_npc_path_best", "pathtype", "mod_test_npc_ai_path_best_type");',
        'getnpcstate("mod_test_npc_path_fixed", "pathtype", "mod_test_npc_ai_path_fixed_type");',
        'getnpcstate("mod_test_npc_path_normal", "pathsearchmaxtry", "mod_test_npc_ai_path_normal_maxtry");',
        'getnpcstate("mod_test_npc_path_enemy", "pathsearchmaxtry", "mod_test_npc_ai_path_enemy_maxtry");',
        'getnpcstate("mod_test_npc_path_best", "pathsearchmaxtry", "mod_test_npc_ai_path_best_maxtry");',
        'getnpcstate("mod_test_npc_path_normal", "usepathfinder", "mod_test_npc_ai_path_normal_use_pathfinder");',
        'getnpcstate("mod_test_npc_path_event", "usepathfinder", "mod_test_npc_ai_path_event_use_pathfinder");',
        'getnpcstate("mod_test_npc_path_enemy", "usepathfinder", "mod_test_npc_ai_path_enemy_use_pathfinder");',
        'getnpcstate("mod_test_npc_path_best", "usepathfinder", "mod_test_npc_ai_path_best_use_pathfinder");',
        'getnpcstate("mod_test_npc_path_fixed", "usepathfinder", "mod_test_npc_ai_path_fixed_use_pathfinder");',
    ]:
        assert_contains(script, required_call, "npc_ai path type script contract")

    fixture_expectations = [
        (make_npc_ai_path_normal_ini(), ["Name=MOD_TEST_NPC_PATH_NORMAL", "Kind=0", "Relation=2"]),
        (make_npc_ai_path_event_ini(), ["Name=MOD_TEST_NPC_PATH_EVENT", "Kind=5", "Relation=2"]),
        (make_npc_ai_path_enemy_ini(), ["Name=MOD_TEST_NPC_PATH_ENEMY", "Kind=1", "Relation=1"]),
        (make_npc_ai_path_best_ini(), ["Name=MOD_TEST_NPC_PATH_BEST", "Kind=1", "Relation=2", "PathFinder=1"]),
        (
            make_npc_ai_path_fixed_ini(),
            [
                "Name=MOD_TEST_NPC_PATH_FIXED",
                "Kind=1",
                "Relation=2",
                "FixedPos=1F000000140000002000000014000000",
                "CurrPos=1",
            ],
        ),
    ]
    for fixture_ini, required_lines in fixture_expectations:
        for required_line in required_lines:
            assert_contains(fixture_ini, required_line, "npc_ai path type fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_npc_ai_path_normal_type=3",
        "mod_test_npc_ai_path_event_type=3",
        "mod_test_npc_ai_path_enemy_type=0",
        "mod_test_npc_ai_path_best_type=2",
        "mod_test_npc_ai_path_fixed_type=0",
        "mod_test_npc_ai_path_normal_maxtry=500",
        "mod_test_npc_ai_path_enemy_maxtry=10",
        "mod_test_npc_ai_path_best_maxtry=100",
        "mod_test_npc_ai_path_normal_use_pathfinder=1",
        "mod_test_npc_ai_path_event_use_pathfinder=1",
        "mod_test_npc_ai_path_enemy_use_pathfinder=0",
        "mod_test_npc_ai_path_best_use_pathfinder=1",
        "mod_test_npc_ai_path_fixed_use_pathfinder=0",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc_ai path type expectation")

    generated_files = scenario_files()
    for required_path in [
        "ini/npc/mod_test_npc_ai_path_normal.ini",
        "ini/npc/mod_test_npc_ai_path_event.ini",
        "ini/npc/mod_test_npc_ai_path_enemy.ini",
        "ini/npc/mod_test_npc_ai_path_best.ini",
        "ini/npc/mod_test_npc_ai_path_fixed.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_npc_ai_attack_interval_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'getnpcstate("mod_test_npc_ai_state", "aitype", "mod_test_npc_ai_state_ai_type");',
        'getnpcstate("mod_test_npc_ai_state", "israndmoverandattack", "mod_test_npc_ai_state_is_rand_move");',
        'getnpcstate("mod_test_npc_ai_state", "isnotfightbackwhenbehit", "mod_test_npc_ai_state_no_fightback");',
        'getnpcstate("mod_test_npc_ai_state", "idle", "mod_test_npc_ai_state_idle");',
        'getnpcstate("mod_test_npc_ai_state", "idledframe", "mod_test_npc_ai_state_idled_frame");',
        'getnpcstate("mod_test_npc_ai_state", "attackspeed", "mod_test_npc_ai_state_attack_speed");',
        'getnpcstate("mod_test_npc_ai_state", "hasattackspeed", "mod_test_npc_ai_state_has_attack_speed");',
        'getnpcstate("mod_test_npc_path_fixed", "hasfixedpath", "mod_test_npc_ai_path_fixed_has_path");',
        'getnpcstate("mod_test_npc_path_fixed", "fixedpathcount", "mod_test_npc_ai_path_fixed_count");',
        'getnpcstate("mod_test_npc_path_fixed", "currentfixedposindex", "mod_test_npc_ai_path_fixed_currpos");',
    ]:
        assert_contains(script, required_call, "npc_ai attack interval script contract")

    state_ini = make_npc_ai_state_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_AI_STATE",
        "AI_TYPE=2",
        "Idle=5",
        "AttackSpeed=8",
    ]:
        assert_contains(state_ini, required_line, "npc_ai attack interval fixture")

    fixed_path_ini = make_npc_ai_path_fixed_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_PATH_FIXED",
        "FixedPos=1F000000140000002000000014000000",
        "CurrPos=1",
    ]:
        assert_contains(fixed_path_ini, required_line, "npc_ai fixed path fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.npc_ai]",
        "EntryScript=mod_test_npc_ai.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "npc_ai attack interval scenario entry",
        )
    for expected_variable in [
        "mod_test_npc_ai_state_ai_type=2",
        "mod_test_npc_ai_state_is_rand_move=1",
        "mod_test_npc_ai_state_no_fightback=1",
        "mod_test_npc_ai_state_idle=5",
        "mod_test_npc_ai_state_idled_frame=0",
        "mod_test_npc_ai_state_attack_speed=8",
        "mod_test_npc_ai_state_has_attack_speed=1",
        "mod_test_npc_ai_path_fixed_has_path=1",
        "mod_test_npc_ai_path_fixed_count=2",
        "mod_test_npc_ai_path_fixed_currpos=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc_ai attack interval expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_npc_ai.txt",
        "ini/npc/mod_test_npc_ai_state.ini",
        "ini/npc/mod_test_npc_ai_path_fixed.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_npc_ai_extension_save_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'displaymessage("正在测试 npc 扩展字段存档读取");',
        'addnpc("mod_test_npc_ai_state.ini", 34, 20, 0);',
        'savenpc("mod_test_npc_ai_extension_save.npc");',
        'loadnpc("mod_test_npc_ai_extension_save.npc");',
        'getnpcstate("mod_test_npc_ai_state", "autorunscript", "mod_test_npc_ai_state_auto_run_script_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "hasautorunscript", "mod_test_npc_ai_state_has_auto_run_script_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "arm", "mod_test_npc_ai_state_arm_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "hasarm", "mod_test_npc_ai_state_has_arm_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "evaden", "mod_test_npc_ai_state_evaden_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "hasevaden", "mod_test_npc_ai_state_has_evaden_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "gengu", "mod_test_npc_ai_state_gengu_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "hasgengu", "mod_test_npc_ai_state_has_gengu_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "neixi", "mod_test_npc_ai_state_neixi_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "hasneixi", "mod_test_npc_ai_state_has_neixi_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "physique", "mod_test_npc_ai_state_physique_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "hasphysique", "mod_test_npc_ai_state_has_physique_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "dodge_beginframe", "mod_test_npc_ai_state_dodge_begin_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "hasdodge_beginframe", "mod_test_npc_ai_state_has_dodge_begin_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "dodge_endframe", "mod_test_npc_ai_state_dodge_end_after_load");',
        'getnpcstate("mod_test_npc_ai_state", "hasdodge_endframe", "mod_test_npc_ai_state_has_dodge_end_after_load");',
    ]:
        assert_contains(script, required_call, "npc_ai extension save script contract")

    state_ini = make_npc_ai_state_ini()
    for required_line in [
        "AutoRunScript=0",
        "Arm=60",
        "EvadeN=61",
        "Gengu=62",
        "Neixi=63",
        "Physique=64",
        "Dodge_BeginFrame=10",
        "Dodge_EndFrame=20",
    ]:
        assert_contains(state_ini, required_line, "npc_ai extension save fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_npc_ai_state_auto_run_script_after_load=0",
        "mod_test_npc_ai_state_has_auto_run_script_after_load=1",
        "mod_test_npc_ai_state_arm_after_load=60",
        "mod_test_npc_ai_state_has_arm_after_load=1",
        "mod_test_npc_ai_state_evaden_after_load=61",
        "mod_test_npc_ai_state_has_evaden_after_load=1",
        "mod_test_npc_ai_state_gengu_after_load=62",
        "mod_test_npc_ai_state_has_gengu_after_load=1",
        "mod_test_npc_ai_state_neixi_after_load=63",
        "mod_test_npc_ai_state_has_neixi_after_load=1",
        "mod_test_npc_ai_state_physique_after_load=64",
        "mod_test_npc_ai_state_has_physique_after_load=1",
        "mod_test_npc_ai_state_dodge_begin_after_load=10",
        "mod_test_npc_ai_state_has_dodge_begin_after_load=1",
        "mod_test_npc_ai_state_dodge_end_after_load=20",
        "mod_test_npc_ai_state_has_dodge_end_after_load=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc_ai extension save expectation")


def test_npc_ai_friend_death_retreat_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'displaymessage("正在测试友方死亡后撤退");',
        'delnpc("mod_test_npc_ai_friend_attacker");',
        'delnpc("mod_test_npc_ai_friend_victim");',
        'delnpc("mod_test_npc_ai_friend_watcher");',
        "setplayerpos(30,23);",
        'addnpc("mod_test_npc_ai_friend_attacker.ini", 31, 20, 0);',
        'addnpc("mod_test_npc_ai_friend_victim.ini", 32, 20, 0);',
        'addnpc("mod_test_npc_ai_friend_watcher.ini", 33, 20, 0);',
        "enablenpcai();",
        'npcusemagic("mod_test_npc_ai_friend_attacker", "mod_test_magic_ai_friend_death_attack.ini", 32, 20, 1);',
        'getnpcstate("mod_test_npc_ai_friend_victim", "life", "mod_test_npc_ai_friend_victim_life");',
        'getnpcstate("mod_test_npc_ai_friend_victim", "currentaction", "mod_test_npc_ai_friend_victim_action_raw");',
        'getnpcstate("mod_test_npc_ai_friend_victim", "ishide", "mod_test_npc_ai_friend_victim_hide");',
        'getnpcstate("mod_test_npc_ai_friend_watcher", "currentaction", "mod_test_npc_ai_friend_watcher_action_raw");',
        'getnpcstate("mod_test_npc_ai_friend_watcher", "mapx", "mod_test_npc_ai_friend_watcher_map_x");',
        'getnpcstate("mod_test_npc_ai_friend_watcher", "steptargetx", "mod_test_npc_ai_friend_watcher_step_x");',
        'getnpcstate("mod_test_npc_ai_friend_watcher", "iswalking", "mod_test_npc_ai_friend_watcher_is_walking");',
        'assign("mod_test_npc_ai_friend_victim_dead", 0);',
        'if getvar("mod_test_npc_ai_friend_victim_life") == 0 then assign("mod_test_npc_ai_friend_victim_dead", 1) end',
        'if getvar("mod_test_npc_ai_friend_victim_action_raw") == 11 then assign("mod_test_npc_ai_friend_victim_dead", 1) end',
        'if getvar("mod_test_npc_ai_friend_victim_hide") == 1 then assign("mod_test_npc_ai_friend_victim_dead", 1) end',
        'assign("mod_test_npc_ai_friend_retreat_away_from_attacker", 0);',
        'if getvar("mod_test_npc_ai_friend_victim_dead") == 1 and getvar("mod_test_npc_ai_friend_watcher_map_x") > 33 then assign("mod_test_npc_ai_friend_retreat_away_from_attacker", 1) end',
        'if getvar("mod_test_npc_ai_friend_victim_dead") == 1 and getvar("mod_test_npc_ai_friend_watcher_step_x") > 33 then assign("mod_test_npc_ai_friend_retreat_away_from_attacker", 1) end',
        'assign("mod_test_npc_ai_friend_retreat_started", 0);',
        'if getvar("mod_test_npc_ai_friend_watcher_is_walking") == 1 then assign("mod_test_npc_ai_friend_retreat_started", 1) end',
        'if getvar("mod_test_npc_ai_friend_watcher_action_raw") == 21 then assign("mod_test_npc_ai_friend_retreat_started", 1) end',
        "disablenpcai();",
        'setnpcpos("mod_test_npc_ai_friend_watcher", 33, 20);',
        'getnpcstate("mod_test_npc_ai_friend_watcher", "currentaction", "mod_test_npc_ai_friend_cached_watcher_action_raw");',
        'getnpcstate("mod_test_npc_ai_friend_watcher", "mapx", "mod_test_npc_ai_friend_cached_watcher_map_x");',
        'getnpcstate("mod_test_npc_ai_friend_watcher", "steptargetx", "mod_test_npc_ai_friend_cached_watcher_step_x");',
        'getnpcstate("mod_test_npc_ai_friend_watcher", "iswalking", "mod_test_npc_ai_friend_cached_watcher_is_walking");',
        'assign("mod_test_npc_ai_friend_retreat_cached_target", 0);',
        'if getvar("mod_test_npc_ai_friend_cached_watcher_map_x") > 33 then assign("mod_test_npc_ai_friend_retreat_cached_target", 1) end',
        'if getvar("mod_test_npc_ai_friend_cached_watcher_step_x") > 33 then assign("mod_test_npc_ai_friend_retreat_cached_target", 1) end',
    ]:
        assert_contains(script, required_call, "npc_ai friend death retreat script contract")

    attacker_ini = make_npc_ai_friend_attacker_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_AI_FRIEND_ATTACKER",
        "Kind=1",
        "Relation=0",
        "Group=211",
        "FlyIni=mod_test_magic_ai_friend_death_attack.ini",
        "Attack=6",
        "Evade=200",
        "VisionRadius=0",
    ]:
        assert_contains(attacker_ini, required_line, "npc_ai friend death attacker fixture")

    victim_ini = make_npc_ai_friend_victim_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_AI_FRIEND_VICTIM",
        "Kind=1",
        "Relation=1",
        "Group=212",
        "Life=5",
        "LifeMax=5",
        "VisionRadius=0",
    ]:
        assert_contains(victim_ini, required_line, "npc_ai friend death victim fixture")

    watcher_ini = make_npc_ai_friend_watcher_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_AI_FRIEND_WATCHER",
        "Kind=1",
        "Relation=1",
        "Group=213",
        "VisionRadius=10",
        "WalkSpeed=4",
        "KeepRadiusWhenFriendDeath=5",
        "NoAutoAttackPlayer=1",
        "StopFindingTarget=1",
    ]:
        assert_contains(watcher_ini, required_line, "npc_ai friend death watcher fixture")

    death_magic_ini = make_magic_ai_friend_death_attack_ini()
    for required_line in [
        "Name=MOD_TEST_AI_FRIEND_DEATH_ATTACK",
        "MoveKind=2",
        "Speed=7",
        "LifeFrame=32",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(death_magic_ini, required_line, "npc_ai friend death attack magic fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.npc_ai]",
        "EntryScript=mod_test_npc_ai.txt",
    ]:
        assert_contains(scenarios_ini, required_line, "npc_ai friend death scenario entry")
    for expected_variable in [
        "mod_test_npc_ai_friend_victim_dead=1",
        "mod_test_npc_ai_friend_retreat_started=1",
        "mod_test_npc_ai_friend_retreat_away_from_attacker=1",
        "mod_test_npc_ai_friend_retreat_cached_target=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc_ai friend death retreat expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_npc_ai.txt",
        "ini/npc/mod_test_npc_ai_friend_attacker.ini",
        "ini/npc/mod_test_npc_ai_friend_victim.ini",
        "ini/npc/mod_test_npc_ai_friend_watcher.ini",
        "ini/magic/mod_test_magic_ai_friend_death_attack.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_npc_partner_destination_cleanup_contract() -> None:
    script = make_npc_ai_script().lower()
    for required_call in [
        'getnpcstate("mod_test_npc_partner", "kind", "mod_test_npc_ai_partner_kind_before");',
        'getnpcstate("mod_test_npc_partner", "relation", "mod_test_npc_ai_partner_relation_before");',
        'getnpcstate("mod_test_npc_partner", "isfighterfriend", "mod_test_npc_ai_partner_is_fighter_friend_before");',
        'getnpcstate("mod_test_npc_partner", "ispartner", "mod_test_npc_ai_partner_is_partner_before");',
        'setnpcdestination("mod_test_npc_partner", 33, 21);',
        'getnpcstate("mod_test_npc_partner", "hasdestination", "mod_test_npc_ai_partner_destination_before");',
        'setnpcpartner("mod_test_npc_partner");',
        'getnpcstate("mod_test_npc_partner", "kind", "mod_test_npc_ai_partner_kind_after");',
        'getnpcstate("mod_test_npc_partner", "relation", "mod_test_npc_ai_partner_relation_after");',
        'getnpcstate("mod_test_npc_partner", "isfighterfriend", "mod_test_npc_ai_partner_is_fighter_friend_after");',
        'getnpcstate("mod_test_npc_partner", "ispartner", "mod_test_npc_ai_partner_is_partner_after");',
        'getnpcstate("mod_test_npc_partner", "isfighter", "mod_test_npc_ai_partner_is_fighter_after");',
        'getnpcstate("mod_test_npc_partner", "hasdestination", "mod_test_npc_ai_partner_destination_after");',
        'getnpcstate("mod_test_npc_partner", "isstanding", "mod_test_npc_ai_partner_standing_after");',
        'getnpcstate("mod_test_npc_partner", "pathtype", "mod_test_npc_ai_partner_path_type_after");',
        'getnpcstate("mod_test_npc_partner", "usepathfinder", "mod_test_npc_ai_partner_use_pathfinder_after");',
    ]:
        assert_contains(script, required_call, "npc_ai SetNpcPartner destination cleanup script contract")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.npc_ai]",
        "EntryScript=mod_test_npc_ai.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "npc_ai SetNpcPartner scenario entry",
        )
    for expected_variable in [
        "mod_test_npc_ai_partner_kind_before=1",
        "mod_test_npc_ai_partner_relation_before=0",
        "mod_test_npc_ai_partner_is_fighter_friend_before=1",
        "mod_test_npc_ai_partner_is_partner_before=0",
        "mod_test_npc_ai_partner_destination_before=1",
        "mod_test_npc_ai_partner_kind_after=3",
        "mod_test_npc_ai_partner_relation_after=0",
        "mod_test_npc_ai_partner_is_fighter_friend_after=1",
        "mod_test_npc_ai_partner_is_partner_after=1",
        "mod_test_npc_ai_partner_is_fighter_after=1",
        "mod_test_npc_ai_partner_destination_after=0",
        "mod_test_npc_ai_partner_standing_after=1",
        "mod_test_npc_ai_partner_path_type_after=2",
        "mod_test_npc_ai_partner_use_pathfinder_after=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc_ai SetNpcPartner destination cleanup expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_npc_ai.txt",
        "ini/npc/mod_test_npc_ai_partner.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_npc_special_action_blocking_contract() -> None:
    script = make_npc_ai_timer_context_script().lower()
    for required_call in [
        'npcspecialaction("mpc049.asf");',
        'getnpcstate("mod_test_npc_ai_timer_context", "isinspecialaction", "mod_test_npc_ai_timer_context_special_nonblocking_active");',
        'npcspecialactionex("mpc049.asf");',
        'getnpcstate("mod_test_npc_ai_timer_context", "isinspecialaction", "mod_test_npc_ai_timer_context_special_ex_after");',
        'npcspecialactionnonblocking("mpc049.asf");',
        'getnpcstate("mod_test_npc_ai_timer_context", "isinspecialaction", "mod_test_npc_ai_timer_context_special_alias_active");',
    ]:
        assert_contains(script, required_call, "npc timer-context special action contract")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.npc_ai]",
        "EntryScript=mod_test_npc_ai.txt",
    ]:
        assert_contains(
            scenarios_ini,
            required_line,
            "npc_ai special action scenario entry",
        )
    for expected_variable in [
        "mod_test_npc_ai_timer_context_special_nonblocking_active=1",
        "mod_test_npc_ai_timer_context_special_returned=1",
        "mod_test_npc_ai_timer_context_special_ex_returned=1",
        "mod_test_npc_ai_timer_context_special_ex_after=0",
        "mod_test_npc_ai_timer_context_special_alias_active=1",
        "mod_test_npc_ai_timer_context_special_alias_returned=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "npc special action expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_npc_ai.txt",
        "script/common/mod_test_npc_ai_timer_context.txt",
        "ini/npc/mod_test_npc_ai_timer_context.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_script_sound_position_contract() -> None:
    script = make_script_sound_position_script().lower()
    for required_call in [
        'playsound("mod_test_script_sound.wav");',
        'getplayerstate("lastscriptsoundhasposition", "mod_test_script_sound_global_has_pos");',
        'getplayerstate("lastscriptsoundsourcetype", "mod_test_script_sound_global_source");',
        'addobj("mod_test_script_sound_obj.ini", 37, 20, 0);',
        'runobjscript("mod_test_script_sound_obj");',
        'addnpc("mod_test_script_sound_npc.ini", 36, 21, 0);',
        'assign("mod_test_script_sound_npc_queued", interactnearestnpc(0, 0, 2));',
    ]:
        assert_contains(script, required_call, "script_sound_position main script")

    object_script = make_script_sound_position_obj_script().lower()
    for required_call in [
        'playsound("mod_test_script_sound.wav");',
        'getplayerstate("lastscriptsoundhasposition", "mod_test_script_sound_obj_has_pos");',
        'getplayerstate("lastscriptsoundsourcetype", "mod_test_script_sound_obj_source");',
        'getplayerstate("lastscriptsoundmapx", "mod_test_script_sound_obj_mapx");',
        'getplayerstate("lastscriptsoundmapy", "mod_test_script_sound_obj_mapy");',
        'getvar("mod_test_script_sound_obj_source") == 2',
        'getvar("mod_test_script_sound_obj_mapx") == 37',
        'getvar("mod_test_script_sound_obj_mapy") == 20',
    ]:
        assert_contains(object_script, required_call, "script_sound_position object script")

    npc_script = make_script_sound_position_npc_script().lower()
    for required_call in [
        'playsound("mod_test_script_sound.wav");',
        'getplayerstate("lastscriptsoundhasposition", "mod_test_script_sound_npc_has_pos");',
        'getplayerstate("lastscriptsoundsourcetype", "mod_test_script_sound_npc_source");',
        'getplayerstate("lastscriptsoundmapx", "mod_test_script_sound_npc_mapx");',
        'getplayerstate("lastscriptsoundmapy", "mod_test_script_sound_npc_mapy");',
        'getvar("mod_test_script_sound_npc_source") == 1',
        'getvar("mod_test_script_sound_npc_mapx") == 36',
        'getvar("mod_test_script_sound_npc_mapy") == 21',
    ]:
        assert_contains(npc_script, required_call, "script_sound_position npc script")

    obj_ini = make_script_sound_position_obj_ini()
    assert_contains(obj_ini, "ObjName=MOD_TEST_SCRIPT_SOUND_OBJ", "script_sound_position obj fixture")
    assert_contains(obj_ini, "ScriptFile=mod_test_script_sound_position_obj.txt", "script_sound_position obj fixture")

    npc_ini = make_script_sound_position_npc_ini()
    assert_contains(npc_ini, "Name=MOD_TEST_SCRIPT_SOUND_NPC", "script_sound_position npc fixture")
    assert_contains(npc_ini, "ScriptFile=mod_test_script_sound_position_npc.txt", "script_sound_position npc fixture")

    probe_wav = make_script_sound_probe_wav()
    if not (probe_wav.startswith("RIFF") and "WAVE" in probe_wav):
        raise AssertionError("script_sound_position probe WAV is not a RIFF/WAVE fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_line in [
        "[Scenario.script_sound_position]",
        "Choice=27",
        "EntryScript=mod_test_script_sound_position.txt",
        "PostNewGameWaitMilliseconds=1200",
        "mod_test_script_sound_global_fallback_ok=1",
        "mod_test_script_sound_obj_source=2",
        "mod_test_script_sound_obj_mapx=37",
        "mod_test_script_sound_obj_mapy=20",
        "mod_test_script_sound_npc_source=1",
        "mod_test_script_sound_npc_mapx=36",
        "mod_test_script_sound_npc_mapy=21",
        "Requires=PlaySound, scriptObj/scriptNPC context propagation, runobjscript, interactnearestnpc, and LastScriptSound readbacks",
    ]:
        assert_contains(scenarios_ini, expected_line, "script_sound_position scenario metadata")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_script_sound_position.txt",
        "script/common/mod_test_script_sound_position_obj.txt",
        "script/common/mod_test_script_sound_position_npc.txt",
        "ini/obj/mod_test_script_sound_obj.ini",
        "ini/npc/mod_test_script_sound_npc.ini",
        "sound/mod_test_script_sound.wav",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_trail_contract() -> None:
    script = make_magic_trail_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_trail.ini");',
        'getmagicstate("mod_test_magic_trail.ini", "movekind", "mod_test_magic_trail_movekind");',
        'getmagicstate("mod_test_magic_trail.ini", "keepmilliseconds", "mod_test_magic_trail_keep_ms");',
        'if getvar("mod_test_magic_trail_movekind") == 19 and getvar("mod_test_magic_trail_keep_ms") == 1200 then assign("mod_test_magic_trail_state", 1) end',
        'usemagic("mod_test_magic_trail.ini", 34, 20);',
        "setplayerpos(35, 20);",
        'geteffectstate("mod_test_magic_trail.ini", "count", "mod_test_magic_trail_first_count");',
        'geteffectstate("mod_test_magic_trail.ini", "mapx", "mod_test_magic_trail_first_x");',
        'geteffectstate("mod_test_magic_trail.ini", "mapy", "mod_test_magic_trail_first_y");',
        'if getvar("mod_test_magic_trail_first_x") == 34 and getvar("mod_test_magic_trail_first_y") == 20 then assign("mod_test_magic_trail_first_position", 1) end',
        "setplayerpos(36, 20);",
        'geteffectstate("mod_test_magic_trail.ini", "count", "mod_test_magic_trail_second_count");',
        "setplayerpos(37, 20);",
        'geteffectstate("mod_test_magic_trail.ini", "count", "mod_test_magic_trail_expired_count");',
        'if getvar("mod_test_magic_trail_expired_count") == getvar("mod_test_magic_trail_second_count") then assign("mod_test_magic_trail_no_after_expire", 1) end',
    ]:
        assert_contains(script, required_call, "magic_trail script contract")

    magic_ini = make_magic_trail_ini()
    for required_line in [
        "Name=MOD_TEST_TRAIL",
        "MoveKind=19",
        "KeepMilliseconds=1200",
    ]:
        assert_contains(magic_ini, required_line, "magic_trail fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_line in [
        "[Scenario.magic_trail]",
        "Choice=29",
        "EntryScript=mod_test_magic_trail.txt",
        "mod_test_magic_trail_state=1",
        "mod_test_magic_trail_first_count=1",
        "mod_test_magic_trail_first_position=1",
        "mod_test_magic_trail_second_count=2",
        "mod_test_magic_trail_no_after_expire=1",
        "Requires=MoveKind=19 trail runtime, KeepMilliseconds, SetPlayerPos tile changes, and GetEffectState effect readbacks",
    ]:
        assert_contains(scenarios_ini, expected_line, "magic_trail scenario metadata")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_trail.txt",
        "ini/magic/mod_test_magic_trail.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_movement_modifier_contract() -> None:
    script = make_magic_lifecycle_script().lower()
    assert_contains(
        script,
        'geteffectstate("mod_test_magic_trace_enemy.ini", "activeprojectileflyingdirectiony", "mod_test_magic_trace_enemy_fly_y_before");\n'
        'sleep(80);\n'
        'geteffectstate("mod_test_magic_trace_enemy.ini", "activeprojectileflyingdirectionx", "mod_test_magic_trace_enemy_fly_x_after");',
        "magic TraceEnemy post-retarget sampling window",
    )
    for required_call in [
        'addmagic("mod_test_magic_random_move.ini");',
        'addmagic("mod_test_magic_meteor.ini");',
        'addmagic("mod_test_magic_trace_enemy.ini");',
        'displaymessage("正在测试追踪敌人的武功");',
        'addnpc("mod_test_collision_target_npc.ini", 34, 34, 0);',
        'usemagic("mod_test_magic_trace_enemy.ini", 42, 20);',
        'geteffectstate("mod_test_magic_trace_enemy.ini", "projectilecount", "mod_test_magic_trace_enemy_projectile_count");',
        'geteffectstate("mod_test_magic_trace_enemy.ini", "activeprojectileflyingdirectionx", "mod_test_magic_trace_enemy_fly_x_before");',
        'geteffectstate("mod_test_magic_trace_enemy.ini", "activeprojectileflyingdirectiony", "mod_test_magic_trace_enemy_fly_y_before");',
        'geteffectstate("mod_test_magic_trace_enemy.ini", "activeprojectileflyingdirectionx", "mod_test_magic_trace_enemy_fly_x_after");',
        'geteffectstate("mod_test_magic_trace_enemy.ini", "activeprojectileflyingdirectiony", "mod_test_magic_trace_enemy_fly_y_after");',
        'assign("mod_test_magic_trace_enemy_far_target", 0);',
        'if getvar("mod_test_magic_trace_enemy_projectile_count") == 1 and getvar("mod_test_magic_trace_enemy_fly_y_after") > 0 and getvar("mod_test_magic_trace_enemy_fly_x_after") < 0 then assign("mod_test_magic_trace_enemy_far_target", 1) end',
        'displaymessage("正在测试陨落型武功");',
        'usemagic("mod_test_magic_meteor.ini", 36, 15);',
        'assign("mod_test_magic_meteor", 1);',
        'geteffectstate("mod_test_magic_meteor.ini", "projectilecount", "mod_test_magic_meteor_projectile_count");',
        'geteffectstate("mod_test_magic_meteor.ini", "activeprojectilemapx", "mod_test_magic_meteor_entry_x");',
        'geteffectstate("mod_test_magic_meteor.ini", "activeprojectilemapy", "mod_test_magic_meteor_entry_y");',
        'assign("mod_test_magic_meteor_entry_path", 0);',
        'if getvar("mod_test_magic_meteor_projectile_count") == 1 and getvar("mod_test_magic_meteor_entry_x") > 0 and getvar("mod_test_magic_meteor_entry_y") > 0 and (getvar("mod_test_magic_meteor_entry_x") ~= 34 or getvar("mod_test_magic_meteor_entry_y") ~= 20) then assign("mod_test_magic_meteor_entry_path", 1) end',
        'displaymessage("正在测试随机移动武功");',
        'usemagic("mod_test_magic_random_move.ini", 34, 12);',
        'geteffectstate("mod_test_magic_random_move.ini", "projectilecount", "mod_test_magic_random_projectile_count");',
        'geteffectstate("mod_test_magic_random_move.ini", "activeprojectileflyingdirectionx", "mod_test_magic_random_fly_x_before");',
        'geteffectstate("mod_test_magic_random_move.ini", "activeprojectileflyingdirectiony", "mod_test_magic_random_fly_y_before");',
        'geteffectstate("mod_test_magic_random_move.ini", "activeprojectileflyingdirectionx", "mod_test_magic_random_fly_x_after");',
        'geteffectstate("mod_test_magic_random_move.ini", "activeprojectileflyingdirectiony", "mod_test_magic_random_fly_y_after");',
        'assign("mod_test_magic_random_direction_changed", 0);',
        'if getvar("mod_test_magic_random_projectile_count") == 1 and getvar("mod_test_magic_random_fly_x_after") ~= getvar("mod_test_magic_random_fly_x_before") then assign("mod_test_magic_random_direction_changed", 1) end',
        'if getvar("mod_test_magic_random_projectile_count") == 1 and getvar("mod_test_magic_random_fly_y_after") ~= getvar("mod_test_magic_random_fly_y_before") then assign("mod_test_magic_random_direction_changed", 1) end',
        'displaymessage("正在测试环绕型武功");',
        'usemagic("mod_test_magic_round.ini", 34, 17);',
        'assign("mod_test_magic_round", 1);',
    ]:
        assert_contains(script, required_call, "magic lifecycle movement modifier contract")

    random_ini = make_magic_random_move_ini()
    for required_line in [
        "Name=MOD_TEST_RANDOM_MOVE",
        "RandomMoveDegree=1000",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(random_ini, required_line, "magic RandomMoveDegree fixture")

    meteor_ini = make_magic_meteor_ini()
    for required_line in [
        "Name=MOD_TEST_METEOR",
        "MeteorMove=6",
        "MeteorMoveDir=5",
        "Image=mag003-牧野流星.asf",
        "FlyingImage=mag003-1-牧野流星.asf",
        "VanishImage=mag003-2-牧野流星.asf",
    ]:
        assert_contains(meteor_ini, required_line, "magic MeteorMove fixture")

    trace_enemy_ini = make_magic_trace_enemy_ini()
    for required_line in [
        "Name=MOD_TEST_TRACE_ENEMY",
        "TraceEnemy=1",
        "TraceSpeed=12",
        "TraceEnemyDelayMilliseconds=100",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(trace_enemy_ini, required_line, "magic TraceEnemy fixture")

    round_ini = make_magic_round_ini()
    for required_line in [
        "Name=MOD_TEST_ROUND",
        "RoundMoveClockwise=1",
        "RoundMoveCount=4",
        "RoundMoveDegreeSpeed=120",
        "RoundRadius=72",
    ]:
        assert_contains(round_ini, required_line, "magic RoundMove standard spelling fixture")
    if "RoundMoveColockwise=" in round_ini:
        raise AssertionError("magic RoundMove fixture should exercise the standard RoundMoveClockwise spelling")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_meteor=1",
        "mod_test_magic_meteor_entry_path=1",
        "mod_test_magic_trace_enemy_far_target=1",
        "mod_test_magic_random_projectile_count=1",
        "mod_test_magic_random_direction_changed=1",
        "mod_test_magic_round=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic lifecycle movement modifier expectation")
    for required_line in [
        "[Scenario.magic_lifecycle]",
        "EntryScript=mod_test_magic_lifecycle.txt",
    ]:
        assert_contains(scenarios_ini, required_line, "magic lifecycle movement modifier scenario entry")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_lifecycle.txt",
        "ini/magic/mod_test_magic_random_move.ini",
        "ini/magic/mod_test_magic_trace_enemy.ini",
        "ini/magic/mod_test_magic_meteor.ini",
        "ini/magic/mod_test_magic_round.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_begin_follow_contract() -> None:
    script = make_magic_lifecycle_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_begin_follow.ini");',
        'displaymessage("正在测试武功起始与跟随效果");',
        'usemagic("mod_test_magic_begin_follow.ini", 34, 14);',
        'assign("mod_test_magic_begin_follow", 1);',
    ]:
        assert_contains(script, required_call, "magic_lifecycle BeginAtMouse/FollowMouse script contract")

    magic_ini = make_magic_begin_follow_ini()
    for required_line in [
        "Name=MOD_TEST_BEGIN_FOLLOW",
        "BeginAtMouse=1",
        "FollowMouse=1",
        "RandomMoveDegree=80",
        "NoExplodeWhenLifeFrameEnd=1",
    ]:
        assert_contains(magic_ini, required_line, "magic_lifecycle BeginAtMouse/FollowMouse fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_begin_follow=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic_lifecycle BeginAtMouse/FollowMouse expectation")

    generated_files = scenario_files()
    magic_path = "ini/magic/mod_test_magic_begin_follow.ini"
    if magic_path not in generated_files:
        raise AssertionError(f"scenario files: missing {magic_path}")


def test_magic_morph_replace_primary_list_contract() -> None:
    script = make_magic_lifecycle_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_morph_replace.ini");',
        'getplayerstate("hasactivereplacemagiclist", "mod_test_magic_morph_replace_before");',
        'getplayerstate("visiblemagiclistcount", "mod_test_magic_morph_visible_before");',
        'usemagic("mod_test_magic_morph_replace.ini", 34, 20);',
        'getplayerstate("hasactivereplacemagiclist", "mod_test_magic_morph_replace_active");',
        'getplayerstate("visiblemagiclistcount", "mod_test_magic_morph_visible_during");',
        'getplayerstate("primarymagiclistcount", "mod_test_magic_morph_primary_during");',
        'if getvar("mod_test_magic_morph_visible_during") == 2 then assign("mod_test_magic_morph_visible_two", 1) end',
        'if getvar("mod_test_magic_morph_primary_during") == getvar("mod_test_magic_morph_visible_before") then assign("mod_test_magic_morph_primary_preserved", 1) end',
        'addmagic("mod_test_magic_post_cast_die.ini");',
        'setmagiclevel("mod_test_magic_post_cast_die.ini", 2);',
        'getplayermagiclevel("mod_test_magic_post_cast_die.ini", "mod_test_magic_morph_primary_added_level");',
        'getplayerstate("visiblemagiclistcount", "mod_test_magic_morph_visible_after_primary_add");',
        'getplayerstate("primarymagiclistcount", "mod_test_magic_morph_primary_after_add");',
        'if getvar("mod_test_magic_morph_primary_after_add") == getvar("mod_test_magic_morph_primary_during") + 1 and getvar("mod_test_magic_morph_primary_added_level") == 2 then assign("mod_test_magic_morph_primary_add_recorded", 1) end',
        'if getvar("mod_test_magic_morph_visible_after_primary_add") == getvar("mod_test_magic_morph_visible_during") then assign("mod_test_magic_morph_visible_add_isolated", 1) end',
        'getplayerstate("hasactivereplacemagiclist", "mod_test_magic_morph_replace_after");',
        'getplayerstate("visiblemagiclistcount", "mod_test_magic_morph_visible_after");',
        'getplayerstate("primarymagiclistcount", "mod_test_magic_morph_primary_after");',
        'if getvar("mod_test_magic_morph_visible_after") == getvar("mod_test_magic_morph_primary_after_add") then assign("mod_test_magic_morph_visible_restored", 1) end',
        'if getvar("mod_test_magic_morph_visible_after") == getvar("mod_test_magic_morph_visible_before") + 1 and getvar("mod_test_magic_morph_primary_after") == getvar("mod_test_magic_morph_primary_after_add") and getvar("mod_test_magic_morph_added_level_after") == 2 then assign("mod_test_magic_morph_primary_add_restored_visible", 1) end',
    ]:
        assert_contains(script, required_call, "magic_lifecycle morph ReplaceMagic primary-list contract")

    magic_ini = make_magic_morph_replace_ini()
    for required_line in [
        "Name=MOD_TEST_MORPH_REPLACE",
        "MoveKind=13",
        "SpecialKind=7",
        "MorphMilliseconds=900",
        "ReplaceMagic=mod_test_magic_begin_follow.ini;mod_test_magic_round.ini",
    ]:
        assert_contains(magic_ini, required_line, "magic_lifecycle morph ReplaceMagic fixture")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.magic_lifecycle]",
        "EntryScript=mod_test_magic_lifecycle.txt",
    ]:
        assert_contains(scenarios_ini, required_line, "magic_lifecycle morph ReplaceMagic scenario entry")
    for expected_variable in [
        "mod_test_magic_morph_replace_before=0",
        "mod_test_magic_morph_replace_active=1",
        "mod_test_magic_morph_visible_two=1",
        "mod_test_magic_morph_primary_preserved=1",
        "mod_test_magic_morph_primary_add_recorded=1",
        "mod_test_magic_morph_visible_add_isolated=1",
        "mod_test_magic_morph_replace_after=0",
        "mod_test_magic_morph_visible_restored=1",
        "mod_test_magic_morph_primary_add_restored_visible=1",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic_lifecycle morph ReplaceMagic expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_lifecycle.txt",
        "ini/magic/mod_test_magic_morph_replace.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_control_death_cleanup_contract() -> None:
    script = make_magic_transport_control_script().lower()
    for required_call in [
        'addmagic("mod_test_magic_control.ini");',
        'setmagiclevel("mod_test_magic_control.ini", 1);',
        'addnpc("mod_test_magic_control_target_npc.ini", 36, 20, 0);',
        'usemagic("mod_test_magic_control.ini", 36, 20);',
        'getplayerstate("iscontrollingcharacter", "mod_test_magic_control_death_active");',
        'getplayerstate("camerafollownpc", "mod_test_magic_control_death_camera_active");',
        'getnpcstate("mod_test_npc_control_target", "iscontrolledbyplayer", "mod_test_magic_control_death_npc_controlled");',
        'setnpcaction("mod_test_npc_control_target", 11);',
        "sleep(120);",
        'getplayerstate("iscontrollingcharacter", "mod_test_magic_control_death_after");',
        'getplayerstate("camerafollownpc", "mod_test_magic_control_death_camera_after");',
        'getnpcstate("mod_test_npc_control_target", "iscontrolledbyplayer", "mod_test_magic_control_death_npc_after");',
    ]:
        assert_contains(script, required_call, "magic_transport_control death cleanup script contract")

    magic_ini = make_magic_control_ini()
    for required_line in [
        "Name=MOD_TEST_CONTROL",
        "MoveKind=21",
        "MaxLevel=5",
    ]:
        assert_contains(magic_ini, required_line, "magic_transport_control control fixture")

    target_ini = make_magic_control_target_npc_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_CONTROL_TARGET",
        "Relation=1",
        "Level=1",
        "Life=9999",
        "LifeMax=9999",
    ]:
        assert_contains(target_ini, required_line, "magic_transport_control target fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "[Scenario.magic_transport_control]",
        "EntryScript=mod_test_magic_transport_control.txt",
        "mod_test_magic_control_death_active=1",
        "mod_test_magic_control_death_camera_active=1",
        "mod_test_magic_control_death_npc_controlled=1",
        "mod_test_magic_control_death_after=0",
        "mod_test_magic_control_death_camera_after=0",
        "mod_test_magic_control_death_npc_after=0",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic_transport_control death cleanup expectation")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_magic_transport_control.txt",
        "ini/magic/mod_test_magic_control.ini",
        "ini/npc/mod_test_magic_control_target_npc.ini",
        "ini/npc/mod_test_magic_control_watcher_npc.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_magic_control_runtime_relation_contract() -> None:
    script = make_magic_transport_control_script().lower()
    for required_call in [
        'delnpc("mod_test_npc_control_watcher");',
        'addnpc("mod_test_magic_control_watcher_npc.ini", 37, 20, 0);',
        'getnpcstate("mod_test_npc_control_watcher", "hascurrentcombattarget", "mod_test_magic_control_watcher_before");',
        'getplayerstate("controlledtargetrelation", "mod_test_magic_control_target_raw_relation");',
        'getplayerstate("controlledtargetruntimerelation", "mod_test_magic_control_target_runtime_relation");',
        'getnpcstate("mod_test_npc_control_target", "runtimerelation", "mod_test_magic_control_npc_runtime_relation");',
        'getnpcstate("mod_test_npc_control_target", "relation", "mod_test_magic_control_npc_raw_relation");',
        'getnpcstate("mod_test_npc_control_target", "isruntimefighterfriend", "mod_test_magic_control_runtime_friend");',
        "enablenpcai();",
        "sleep(120);",
        'getnpcstate("mod_test_npc_control_watcher", "hascurrentcombattarget", "mod_test_magic_control_watcher_active");',
        'getnpcstate("mod_test_npc_control_watcher", "currentcombattargetmapx", "mod_test_magic_control_watcher_target_x");',
        'getnpcstate("mod_test_npc_control_watcher", "currentcombattargetmapy", "mod_test_magic_control_watcher_target_y");',
        'assign("mod_test_magic_control_watcher_target_match", 0);',
        'if getvar("mod_test_magic_control_watcher_target_x") == 36 and getvar("mod_test_magic_control_watcher_target_y") == 20 then assign("mod_test_magic_control_watcher_target_match", 1) end',
        "disablenpcai();",
        "sleep(900);",
        'getnpcstate("mod_test_npc_control_target", "runtimerelation", "mod_test_magic_control_npc_runtime_after");',
        'getnpcstate("mod_test_npc_control_watcher", "hascurrentcombattarget", "mod_test_magic_control_watcher_after");',
    ]:
        assert_contains(script, required_call, "magic_transport_control runtime relation script contract")

    watcher_ini = make_magic_control_watcher_npc_ini()
    for required_line in [
        "Name=MOD_TEST_NPC_CONTROL_WATCHER",
        "Relation=1",
        "AttackLevel=1",
        "AttackRadius=3",
        "AttackSpeed=8",
        "VisionRadius=8",
        "NoAutoAttackPlayer=0",
        "StopFindingTarget=0",
    ]:
        assert_contains(watcher_ini, required_line, "magic_transport_control watcher fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_variable in [
        "mod_test_magic_control_target_raw_relation=1",
        "mod_test_magic_control_target_runtime_relation=0",
        "mod_test_magic_control_npc_runtime_relation=0",
        "mod_test_magic_control_npc_raw_relation=1",
        "mod_test_magic_control_runtime_friend=1",
        "mod_test_magic_control_watcher_before=0",
        "mod_test_magic_control_watcher_active=1",
        "mod_test_magic_control_watcher_target_match=1",
        "mod_test_magic_control_npc_runtime_after=1",
        "mod_test_magic_control_watcher_after=0",
        "controlled-target runtime relation recovery",
    ]:
        assert_contains(scenarios_ini, expected_variable, "magic_transport_control runtime relation expectation")

    generated_files = scenario_files()
    watcher_path = "ini/npc/mod_test_magic_control_watcher_npc.ini"
    if watcher_path not in generated_files:
        raise AssertionError(f"scenario files: missing {watcher_path}")


def test_time_stop_visual_repro_contract() -> None:
    script = make_time_stop_visual_repro_script().lower()
    for required_call in [
        'displaymessage("时间停止画面复现开始");',
        'assign("mod_test_time_stop_visual_repro_ready", 1);',
        'loadmap("map001_',
        'loadnpc("map001.npc");',
        'loadobj("map001.obj");',
        "setplayerpos(34,20);",
        "setplayerdir(6);",
        "fullmana();",
        "fullthew();",
        'addmagic("mod_test_magic_time_stop_visual.ini");',
        'delnpc("mod_test_time_stop_walker");',
        'addnpc("mod_test_time_stop_visual_walker.ini", 31, 20, 0);',
        'setnpcdestination("mod_test_time_stop_walker", 36, 20);',
        "sleep(80);",
        'displaymessage("时间停止检查：行走 npc 的位置和动画帧都应冻结");',
        'usemagic("mod_test_magic_time_stop_visual.ini", 34, 20);',
        'assign("mod_test_time_stop_visual_repro_cast", 1);',
    ]:
        assert_contains(script, required_call, "time-stop visual repro script contract")

    magic_ini = make_magic_time_stop_visual_ini()
    for required_line in [
        "Name=MOD_TEST_TIMESTOP_VISUAL",
        "MoveKind=23",
        "SpecialKind=6",
        "WaitFrame=0",
        "LifeFrame=250",
        "NoExplodeWhenLifeFrameEnd=1",
        "Image=mag009-\u98ce\u96f7\u4e5d\u5dde.asf",
        "Icon=mag009-\u98ce\u96f7\u4e5d\u5ddes.asf",
        "FlyingImage=mag009-1-\u98ce\u96f7\u4e5d\u5dde.asf",
        "VanishImage=mag009-1-\u98ce\u96f7\u4e5d\u5dde.asf",
        "[Level1]",
    ]:
        assert_contains(magic_ini, required_line, "time-stop visual magic fixture")

    for stale_fragment in [
        "\u690b\u5eaf",
        "\u6d44",
        "\u6d7c",
        "\u8924",
    ]:
        if stale_fragment in magic_ini:
            raise AssertionError(f"time-stop visual magic fixture: stale mojibake fragment {stale_fragment!r}")

    walker_ini = make_time_stop_visual_mover_ini()
    for required_line in [
        "Name=MOD_TEST_TIME_STOP_WALKER",
        "Kind=1",
        "Relation=3",
        "Group=262",
        "Dir=2",
        "TalkContent=MOD_TEST time-stop visual walker fixture",
    ]:
        assert_contains(walker_ini, required_line, "time-stop visual walker fixture")

    scenarios_ini = make_scenarios_ini()
    for expected_line in [
        "[Scenario.time_stop_visual_repro]",
        "Choice=32",
        "Smoke=0",
        "EntryScript=mod_test_time_stop_visual_repro.txt",
        "mod_test_time_stop_visual_repro_ready=1",
        "mod_test_time_stop_visual_repro_cast=1",
        "visible window or computer-use inspection for animation-frame acceptance",
    ]:
        assert_contains(scenarios_ini, expected_line, "time-stop visual scenario metadata")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_time_stop_visual_repro.txt",
        "ini/magic/mod_test_magic_time_stop_visual.ini",
        "ini/npc/mod_test_time_stop_visual_walker.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"scenario files: missing {required_path}")


def test_gamble_not_game_type_gated_contract() -> None:
    script = make_gamble_script().lower()
    for required_call in [
        'assign("__automation_gamble_enabled", 1);',
        'gamble(80, 0, "mod_test_gamble_loss_result");',
        'gamble(80, 0, "mod_test_gamble_leave_result");',
    ]:
        assert_contains(script, required_call, "script_gamble automation contract")

    scenarios_ini = make_scenarios_ini()
    assert_contains(
        scenarios_ini,
        "Requires=Gamble API, player money, and __automation_gamble_* test variables",
        "script_gamble requirement metadata",
    )
    if "Requires=Game.Type XJXQY" in scenarios_ini:
        raise AssertionError("script_gamble must not require a Game.Type-gated GambleMenu")


def test_dice_and_fish_gui_scenarios_load_save_before_modal() -> None:
    for script_name, script, modal_call, result_assignment in [
        (
            "script_dice_game",
            make_dice_script().lower(),
            'showdicegame("掷骰测试");',
            'assign("mod_test_dice_opened", 1);',
        ),
        (
            "script_fish_game",
            make_fish_script().lower(),
            "showfishgame();",
            'assign("mod_test_fish_opened", 1);',
        ),
    ]:
        load_index = script.find("loadgame(0);")
        skip_index = script.find('assign("mod_test_skip_base_newgame", 1);')
        modal_index = script.find(modal_call)
        result_index = script.find(result_assignment)
        if not (0 <= load_index < skip_index < modal_index < result_index):
            raise AssertionError(
                f"{script_name}: save load and skip flag must precede the modal call, "
                "and the expected result must be assigned after it closes"
            )


def test_choose_menu_layout_and_index_contract() -> None:
    automated_script = make_choose_ex_plus_script()
    for required_line in [
        'assign("__automation_choose_selection", 3);',
        'chooseex("扩展选择框测试", "{0}隐藏选项", "", "甲选项", "{mod_test_choose_ex_show_beta}乙选项", "丙选项", "mod_test_choose_ex_result");',
        'if getvar("mod_test_choose_ex_result") == 3 then assign("mod_test_choose_ex_result_ok", 1) end',
        'assign("__automation_choose_selection", 2);',
        'chooseplus("#name", 2, 0, "增强选择框测试", "{0}隐藏选项", "", "目标选项", "其他选项", "mod_test_choose_plus_result");',
        'if getvar("mod_test_choose_plus_result") == 2 then assign("mod_test_choose_plus_result_ok", 1) end',
    ]:
        assert_contains(automated_script, required_line, "choose empty-option original-index contract")

    visual_script = make_choose_menu_visual_script()
    for required_fragment in [
        'chooseplus("#name", 2, 0,',
        '<enter>第二行检查自动换行与面板增长<enter>第三行',
        '"", "选项一"',
        '"选项十一：请选择这个原始末尾索引"',
        'getvar("mod_test_choose_menu_visual_result") == 11',
        'chooseplus("酒肆老板", -1, 1,',
    ]:
        assert_contains(visual_script, required_fragment, "choose menu visual script")

    runner_script = make_runner_script()
    for required_line in [
        'if getvar("mod_test_manual_menu_choice") == 32 then assign("mod_test_scenario_choice", 33); goto ChooseMenuVisual end',
        "::ChooseMenuVisual::",
        'runscript("mod_test_choose_menu_visual.txt");',
    ]:
        assert_contains(runner_script, required_line, "choose menu visual runner")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.choose_menu_visual]",
        "Choice=33",
        "Smoke=0",
        "EntryScript=mod_test_choose_menu_visual.txt",
        "mod_test_choose_menu_visual_result=11",
        "mod_test_choose_menu_visual_right_result=0",
        "computer-use inspection",
    ]:
        assert_contains(scenarios_ini, required_line, "choose menu visual scenario metadata")

    generated_files = scenario_files()
    if "script/common/mod_test_choose_menu_visual.txt" not in generated_files:
        raise AssertionError("scenario files: missing script/common/mod_test_choose_menu_visual.txt")


def test_player_facing_test_mod_text_is_localized() -> None:
    args = parse_args([])
    if args.name != "新剑侠情缘综合测试 MOD":
        raise AssertionError(f"unexpected default test MOD name: {args.name!r}")
    if args.author != "":
        raise AssertionError(f"unexpected default test MOD author: {args.author!r}")

    runner_script = make_runner_script()
    for required_text in [
        'displaymessage("正在打开测试场景向导；测试广场位于第一项，原版开局位于最后一项");',
        'chooseex("测试场景向导", "交互测试广场", "赌博测试", "掷骰测试"',
        '"多项选择", "V 型区域武功", "时间脚本与并行脚本"',
        '"时间停止画面复现", "选择框画面测试", "爆炸子武功"',
        '"手动武功训练场", "手动闭环验证", "进入新剑原版开局", "mod_test_manual_menu_choice"',
    ]:
        assert_contains(runner_script, required_text, "localized test scenario guide")

    generated_files = scenario_files()
    for path, content in generated_files.items():
        if path.startswith("script/common/") and 'displaymessage("MOD_TEST' in content:
            raise AssertionError(f"player-facing English diagnostic remains in {path}")

    localized_surface = "\n".join(
        [
            runner_script,
            make_choose_ex_plus_script(),
            make_choose_menu_visual_script(),
            make_dice_script(),
        ]
    )
    for legacy_text in [
        "MOD_TEST scenario",
        "Continue to base game",
        '"Gamble"',
        "MOD_TEST choose ex",
        "Option 11 - select this final original index",
        "Finish visual acceptance",
        'showdicegame("MOD_TEST")',
    ]:
        if legacy_text in localized_surface:
            raise AssertionError(f"legacy player-facing English text remains: {legacy_text!r}")


def test_profile_author_is_opt_in() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = make_minimal_assets(Path(temp_dir))
        base_pack = discover_base_pack(assets_root, "XJXQY")

        default_args = parse_args([])
        default_args.save_namespace = default_args.pack_id
        default_profile = make_profile(default_args, base_pack, default_args.pack_path)
        if "\nAuthor=" in default_profile:
            raise AssertionError("default test MOD profile unexpectedly writes Game.Author")
        assert_contains(
            default_profile,
            "\n[Combat]\nMinimumMagicDamage=5\n",
            "base minimum magic damage profile inheritance",
        )
        assert_contains(
            default_profile,
            "\nTextEncodingConverted=1\n",
            "generated UTF-8 profile marker",
        )

        authored_args = parse_args(["--author", "测试包作者"])
        authored_args.save_namespace = authored_args.pack_id
        authored_profile = make_profile(authored_args, base_pack, authored_args.pack_path)
        assert_contains(authored_profile, "\nAuthor=测试包作者\n", "explicit test MOD author")


def test_manual_magic_arena_contract() -> None:
    runner_script = make_runner_script()
    for required_line in [
        '"动画参数画面测试", "视频与游戏后台继续", "MG 暴击反馈", "施法者离场弹体", "手动武功训练场", "手动闭环验证"',
        'if getvar("mod_test_manual_menu_choice") == 39 then assign("mod_test_scenario_choice", 40); goto ManualMagicArena end',
        'if getvar("mod_test_manual_menu_choice") == 40 then assign("mod_test_scenario_choice", 41); goto ManualContinuity end',
        "::ManualMagicArena::",
        'runscript("mod_test_manual_magic_arena.txt");',
        "::ManualContinuity::",
        'runscript("mod_test_manual_continuity.txt");',
    ]:
        assert_contains(runner_script, required_line, "manual magic arena runner")

    arena_script = make_manual_magic_arena_script()
    for required_line in [
        'loadmap("map001_衡山.map");',
        'addnpc("mod_test_manual_arena_target_center.ini", 34, 15, 0);',
        'addnpc("mod_test_manual_arena_target_left.ini", 32, 16, 0);',
        'addnpc("mod_test_manual_arena_target_right.ini", 36, 16, 0);',
        'addnpc("mod_test_manual_arena_trainer.ini", 37, 21, 4);',
        "centercamera();",
        'assign("mod_test_manual_magic_arena_ready", 1);',
    ]:
        assert_contains(arena_script, required_line, "manual magic arena setup")
    if 'loadnpc("map001.npc")' in arena_script:
        raise AssertionError("manual magic arena must not load unrelated production NPCs")
    magic_definitions = manual_magic_arena_magic_definitions()
    if len(magic_definitions) != 14:
        raise AssertionError(
            f"manual magic arena: expected 14 magic definitions, got {len(magic_definitions)}"
        )
    if len(set(manual_magic_arena_magic_names())) != len(magic_definitions):
        raise AssertionError("manual magic arena: duplicate generated magic file name")

    icons = [icon for _, _, _, icon, _ in magic_definitions]
    if len(set(icons)) != len(magic_definitions):
        raise AssertionError("manual magic arena: each magic must use a distinct icon")

    for magic_name in manual_magic_arena_magic_names():
        assert_contains(arena_script, f'addmagic("{magic_name}");', "manual magic arena grant")

    target_ini = make_manual_magic_arena_target_npc_ini(
        "MOD_TEST_ARENA_TARGET", "manual target"
    )
    for required_line in [
        "NpcIni=npcres018_金兀术.ini",
        "Relation=1",
        "Life=99999",
        "LifeMax=99999",
        "Attack=1",
        "Defence=500",
        "NoAutoAttackPlayer=1",
        "StopFindingTarget=1",
    ]:
        assert_contains(target_ini, required_line, "manual magic arena target")

    trainer_ini = make_manual_magic_arena_trainer_npc_ini()
    assert_contains(
        trainer_ini,
        "ScriptFile=mod_test_manual_magic_arena_trainer.txt",
        "manual magic arena trainer NPC",
    )
    trainer_script = make_manual_magic_arena_trainer_script()
    for required_fragment in [
        'chooseplus("#name", 2, 0,',
        "重置木桩与玩家位置",
        "补满生命、内力和体力",
        "清除场上武功特效",
        "重新授予全部测试武功",
        "查看操作说明",
        "返回交互测试广场",
        "::ResetArena::",
        "::GrantMagic::",
        "::ShowHelp::",
        "::ReturnHub::",
        'runscript("mod_test_bootstrap.txt");',
    ]:
        assert_contains(trainer_script, required_fragment, "manual magic arena trainer")
    if "return;\n::" in trainer_script:
        raise AssertionError("manual magic arena trainer labels must precede the only return")

    continuity_script = scaffold_module.make_manual_continuity_script()
    for required_fragment in [
        'runscript("mod_test_manual_magic_arena.txt");',
        'assign("__automation_choose_selection", 5);',
        'runscript("mod_test_manual_magic_arena_trainer.txt");',
        'if getvar("mod_test_ready") == 1 and getvar("mod_test_hub_respawn_count") == 1 then assign("mod_test_manual_continuity_pass", 1) end',
    ]:
        assert_contains(continuity_script, required_fragment, "manual continuity loop")

    scenarios_ini = make_scenarios_ini()
    for required_line in [
        "[Scenario.manual_magic_arena]",
        "Choice=40",
        "Smoke=1",
        "EntryScript=mod_test_manual_magic_arena.txt",
        "ExpectVariables=mod_test_manual_magic_arena_ready=1",
        "Life=99999 Defence=500 Attack=1",
        "[Scenario.manual_continuity]",
        "Choice=41",
        "EntryScript=mod_test_manual_continuity.txt",
        "ExpectVariables=mod_test_manual_continuity_pass=1;mod_test_ready=1;mod_test_hub_respawn_count=1",
    ]:
        assert_contains(scenarios_ini, required_line, "manual magic arena metadata")

    generated_files = scenario_files()
    for required_path in [
        "script/common/mod_test_manual_magic_arena.txt",
        "script/common/mod_test_manual_magic_arena_trainer.txt",
        "script/common/mod_test_manual_continuity.txt",
        "ini/npc/mod_test_manual_arena_target_center.ini",
        "ini/npc/mod_test_manual_arena_target_left.ini",
        "ini/npc/mod_test_manual_arena_target_right.ini",
        "ini/npc/mod_test_manual_arena_trainer.ini",
    ]:
        if required_path not in generated_files:
            raise AssertionError(f"manual magic arena: missing {required_path}")

    center_target_ini = generated_files[
        "ini/npc/mod_test_manual_arena_target_center.ini"
    ]
    assert_contains(
        center_target_ini,
        "NpcIni=npcres059_金兵(刀兵).ini",
        "manual magic arena hurt target",
    )
    for target_path in [
        "ini/npc/mod_test_manual_arena_target_left.ini",
        "ini/npc/mod_test_manual_arena_target_right.ini",
    ]:
        assert_contains(
            generated_files[target_path],
            "NpcIni=npcres018_金兀术.ini",
            f"manual magic arena comparison target {target_path}",
        )

    hurt_resource_path = (
        REPO_ROOT
        / "assets"
        / "xjxqy"
        / "ini"
        / "npcres"
        / "npcres059_金兵(刀兵).ini"
    )
    if not hurt_resource_path.is_file():
        raise AssertionError(
            f"manual magic arena: missing Hurt animation resource {hurt_resource_path}"
        )
    hurt_resource = hurt_resource_path.read_text(encoding="utf-8")
    assert_contains(hurt_resource, "[Hurt]", "manual magic arena Hurt animation")
    assert_contains(
        hurt_resource,
        "Image=npc059_bat.asf",
        "manual magic arena Hurt animation image",
    )

    for file_name, display_name, intro, icon, _ in magic_definitions:
        if not any("\u4e00" <= character <= "\u9fff" for character in display_name):
            raise AssertionError(f"manual magic arena: non-Chinese display name {display_name!r}")
        if not any("\u4e00" <= character <= "\u9fff" for character in intro):
            raise AssertionError(f"manual magic arena: non-Chinese intro for {file_name}")
        if "MOD_TEST" in display_name or "MOD test" in intro:
            raise AssertionError(f"manual magic arena: fixture text leaked into {file_name}")

        magic_path = f"ini/magic/{file_name}"
        if magic_path not in generated_files:
            raise AssertionError(f"manual magic arena: missing {magic_path}")
        magic_ini = generated_files[magic_path]
        assert_contains(magic_ini, f"Name={display_name}", magic_path)
        assert_contains(magic_ini, f"Intro={intro}", magic_path)
        assert_contains(magic_ini, f"Icon={icon}", magic_path)

        icon_path = REPO_ROOT / "assets" / "xjxqy" / "asf" / "magic" / icon
        if not icon_path.is_file():
            raise AssertionError(f"manual magic arena: missing production icon {icon_path}")

    behavior_markers = {
        "mod_test_manual_magic_mouse_guide.ini": "BeginAtMouse=1",
        "mod_test_manual_magic_time_stop.ini": "MoveKind=23",
        "mod_test_manual_magic_carry.ini": "CarryUser=4",
        "mod_test_manual_magic_v_region.ini": "Region=5",
        "mod_test_manual_magic_friend.ini": "ChangeToFriendMilliseconds=1600",
    }
    for file_name, marker in behavior_markers.items():
        assert_contains(
            generated_files[f"ini/magic/{file_name}"],
            marker,
            f"manual magic arena behavior clone {file_name}",
        )


def test_steal_manual_and_automation_entry_contract() -> None:
    runner_script = make_runner_script()
    for required_fragment in [
        '::Steal::\nassign("mod_test_steal_interactive", 1);\nrunscript("mod_test_steal.txt");\nassign("mod_test_steal_interactive", 0);\ngoto ManualMenu',
        '::AutoSteal::\nassign("mod_test_steal_interactive", 0);\nrunscript("mod_test_steal.txt");\ngoto End',
    ]:
        assert_contains(runner_script, required_fragment, "steal runner entry")

    steal_script = make_steal_script()
    for required_fragment in [
        'if getvar("mod_test_steal_interactive") == 1 then goto Interactive end',
        "::Interactive::",
        'assign("__automation_choose_enabled", 0);',
        'assign("__automation_choose_selection", -1);',
        'showstealwin("MOD_TEST_STEAL_NPC", "mod_test_steal_success.txt", "mod_test_steal_fail.txt");',
        'displaymessage("手动偷窃测试结束");',
        "::Automated::",
        'assign("__automation_choose_enabled", 1);',
        'displaymessage("偷窃测试完成");',
        "::End::\nreturn;",
    ]:
        assert_contains(steal_script, required_fragment, "steal scenario mode split")

    interactive_script, automated_script = steal_script.split("::Automated::", 1)
    if interactive_script.count("showstealwin(") != 1:
        raise AssertionError("manual steal scenario must open exactly one interactive menu")
    if automated_script.count("showstealwin(") != 3:
        raise AssertionError("automated steal scenario must retain three smoke checks")
    if "return;\n::" in steal_script:
        raise AssertionError("steal scenario labels must precede the only return")


def test_time_script_has_explicit_test_mod_feedback() -> None:
    runner_script = make_runner_script()
    assert_contains(
        runner_script,
        '"时间脚本与并行脚本"',
        "time script scenario menu entry",
    )

    timer_script = scaffold_module.make_script_timer_parallel_script()
    for required_fragment in [
        'displaymessage("时间脚本与并行脚本测试开始：70 秒倒计时结束时触发时间脚本");',
        'settimescript(0, "mod_test_script_timer_trigger.txt");',
        "opentimelimit(70);",
        'displaymessage("时间脚本与并行脚本测试已启动");',
    ]:
        assert_contains(timer_script, required_fragment, "time script scenario")

    trigger_script = scaffold_module.make_script_timer_trigger_script()
    for required_fragment in [
        'assign("mod_test_script_timer_triggered", 1);',
        'displaymessage("70 秒倒计时结束，时间脚本已触发");',
    ]:
        assert_contains(trigger_script, required_fragment, "time script trigger feedback")

    scenarios_ini = scaffold_module.make_scenarios_ini()
    for required_fragment in [
        "[Scenario.script_timer_parallel]",
        "TimeoutSeconds=100",
        "PostNewGameWaitMilliseconds=72000",
        "SetTimeScript fires as the countdown reaches zero",
    ]:
        assert_contains(scenarios_ini, required_fragment, "time script smoke metadata")


def test_interactive_hub_contract() -> None:
    newgame_script = scaffold_module.make_newgame_script()
    assert_contains(
        newgame_script,
        'runscript("mod_test_runner.txt");',
        "normal and automated newgame runner entry",
    )
    if 'runscript("mod_test_bootstrap.txt");' in newgame_script:
        raise AssertionError("normal test MOD launch must not bypass the complete runner")

    runner_script = make_runner_script()
    for required_fragment in [
        '"交互测试广场", "赌博测试", "掷骰测试"',
        '"手动闭环验证", "进入新剑原版开局", "mod_test_manual_menu_choice"',
        'if getvar("mod_test_manual_menu_choice") < 0 then goto End end',
        'if getvar("mod_test_manual_menu_choice") == 41 then goto BaseGame end',
        'if getvar("mod_test_manual_menu_choice") == 0 then assign("mod_test_scenario_choice", 1); goto Bootstrap end',
        "::Bootstrap::\n" 'runscript("mod_test_bootstrap.txt");\n' "goto End",
        "::AutoBootstrap::\n" 'runscript("mod_test_bootstrap.txt");\n' "goto End",
        "::BaseGame::\n" 'assign("mod_test_base_newgame_requested", 1);\n' 'runscript("newgame.txt");',
    ]:
        assert_contains(runner_script, required_fragment, "continuous manual runner")

    bootstrap_script = scaffold_module.make_bootstrap_script()
    for required_line in [
        'loadmap("map001_衡山.map");',
        'loadobj("map001.obj");',
        "centercamera();",
        'addnpc("mod_test_hub_guide.ini", 33, 20, 0);',
        'addnpc("mod_test_hub_little_games_host.ini", 35, 20, 0);',
        'assign("mod_test_hub_respawn_count", 0);',
        'runscript("mod_test_hub_respawn.txt");',
        'getnpcstate("试炼剑客", "IsPartner", "mod_test_hub_partner_sword_ready");',
        'getnpcstate("试炼剑客", "AttackSpeed", "mod_test_hub_partner_sword_attack_speed");',
        'getnpcstate("试炼女侠", "AttackSpeed", "mod_test_hub_partner_heroine_attack_speed");',
        'getnpcstate("试炼剑客", "MapX", "mod_test_hub_partner_sword_x");',
        'getnpcstate("试炼女侠", "MapX", "mod_test_hub_partner_heroine_x");',
        'getnpcstate("演武刀客", "AttackSpeed", "mod_test_hub_enemy_melee_attack_speed");',
        'getnpcstate("演武刀客", "NoAutoAttackPlayer", "mod_test_hub_enemy_melee_no_auto");',
        'getnpcstate("演武弓手", "AttackSpeed", "mod_test_hub_enemy_ranged_attack_speed");',
        'getnpcstate("演武弓手", "StopFindingTarget", "mod_test_hub_enemy_ranged_stop_find");',
        'assign("mod_test_ready", 1);',
        'displaymessage("测试广场已就绪：点击测试向导或小游戏掌柜交谈");',
    ]:
        assert_contains(bootstrap_script, required_line, "interactive hub bootstrap")
    if 'loadnpc("map001.npc")' in bootstrap_script:
        raise AssertionError("interactive hub must not load inert production map NPCs")

    respawn_script = scaffold_module.make_test_hub_respawn_script()
    for required_line in [
        'addnpc("mod_test_hub_partner_sword.ini", 31, 22, 0);',
        'addnpc("mod_test_hub_partner_heroine.ini", 37, 22, 0);',
        'setnpcpartner("试炼剑客");',
        'setnpcpartner("试炼女侠");',
        'setnpcpos("试炼剑客", 33, 22);',
        'setnpcpos("试炼女侠", 35, 22);',
        'addnpc("mod_test_hub_enemy_melee.ini", 31, 14, 4);',
        'addnpc("mod_test_hub_enemy_ranged.ini", 37, 14, 4);',
        "enablepartnercombat();",
        "enablenpcai();",
    ]:
        assert_contains(respawn_script, required_line, "interactive hub respawn")
    sword_partner_index = respawn_script.index('setnpcpartner("试炼剑客");')
    sword_position_index = respawn_script.index('setnpcpos("试炼剑客", 33, 22);')
    heroine_partner_index = respawn_script.index('setnpcpartner("试炼女侠");')
    heroine_position_index = respawn_script.index('setnpcpos("试炼女侠", 35, 22);')
    ai_enable_index = respawn_script.index("enablenpcai();")
    if not (
        sword_partner_index < sword_position_index < ai_enable_index
        and heroine_partner_index < heroine_position_index < ai_enable_index
    ):
        raise AssertionError(
            "interactive hub must reposition each partner after conversion and before enabling AI"
        )

    little_games_script = scaffold_module.make_test_hub_little_games_script()
    for required_fragment in [
        "请选择要手动操作的小游戏",
        'assign("__automation_gamble_enabled", 0);',
        'gamble(80, 0, "mod_test_hub_gamble_result");',
        'showdicegame("测试广场");',
        "showfishgame();",
    ]:
        assert_contains(little_games_script, required_fragment, "interactive hub little games")

    guide_script = scaffold_module.make_test_hub_guide_script()
    for required_fragment in [
        'chooseplus("测试向导", 2, 0,',
        "这里不会继续原版剧情",
        "打开小游戏菜单",
        "打开专项测试场景",
        "重置伙伴和敌人",
        "进入武功训练场",
        'runscript("mod_test_hub_little_games.txt");',
        'runscript("mod_test_runner.txt");',
        'runscript("mod_test_hub_respawn.txt");',
        'runscript("mod_test_manual_magic_arena.txt");',
    ]:
        assert_contains(guide_script, required_fragment, "interactive hub guide")
    if "return;\n::" in guide_script:
        raise AssertionError("interactive hub guide labels must precede the only return")
    if "return;\n::" in little_games_script:
        raise AssertionError("interactive hub little-games labels must precede the only return")

    generated_files = scenario_files()
    required_paths = [
        "script/common/mod_test_hub_respawn.txt",
        "script/common/mod_test_hub_guide.txt",
        "script/common/mod_test_hub_little_games.txt",
        "ini/npc/mod_test_hub_guide.ini",
        "ini/npc/mod_test_hub_little_games_host.ini",
        "ini/npc/mod_test_hub_partner_sword.ini",
        "ini/npc/mod_test_hub_partner_heroine.ini",
        "ini/npc/mod_test_hub_enemy_melee.ini",
        "ini/npc/mod_test_hub_enemy_ranged.ini",
    ]
    for required_path in required_paths:
        if required_path not in generated_files:
            raise AssertionError(f"interactive hub: missing {required_path}")

    assert_contains(
        generated_files["ini/npc/mod_test_hub_guide.ini"],
        "ScriptFile=mod_test_hub_guide.txt",
        "interactive hub guide NPC",
    )
    assert_contains(
        generated_files["ini/npc/mod_test_hub_little_games_host.ini"],
        "ScriptFile=mod_test_hub_little_games.txt",
        "interactive hub little-games NPC",
    )
    assert_contains(
        generated_files["ini/npc/mod_test_hub_enemy_ranged.ini"],
        "NpcIni=npcres053_成都杀手远程.ini",
        "interactive hub ranged enemy resource",
    )
    for combat_fixture_path in (
        "ini/npc/mod_test_hub_partner_sword.ini",
        "ini/npc/mod_test_hub_partner_heroine.ini",
        "ini/npc/mod_test_hub_enemy_melee.ini",
        "ini/npc/mod_test_hub_enemy_ranged.ini",
    ):
        combat_fixture = generated_files[combat_fixture_path]
        assert_contains(combat_fixture, "AttackSpeed=1", combat_fixture_path)
        if "AttackSpeed=7" in combat_fixture or "AttackSpeed=8" in combat_fixture:
            raise AssertionError(
                f"{combat_fixture_path}: manual plaza fixture retains accelerated attack speed"
            )
    ranged_resource_path = (
        REPO_ROOT / "assets" / "xjxqy" / "ini" / "npcres" / "npcres053_成都杀手远程.ini"
    )
    if not ranged_resource_path.is_file():
        raise AssertionError(
            f"interactive hub ranged enemy resource is missing: {ranged_resource_path}"
        )
    for enemy_path in (
        "ini/npc/mod_test_hub_enemy_melee.ini",
        "ini/npc/mod_test_hub_enemy_ranged.ini",
    ):
        assert_contains(generated_files[enemy_path], "Relation=1", enemy_path)
        assert_contains(generated_files[enemy_path], "NoAutoAttackPlayer=0", enemy_path)
        assert_contains(generated_files[enemy_path], "StopFindingTarget=0", enemy_path)
        assert_contains(generated_files[enemy_path], "VisionRadius=8", enemy_path)

    scenarios_ini = make_scenarios_ini()
    assert_contains(
        scenarios_ini,
        "mod_test_hub_respawn_count=1;mod_test_hub_partner_sword_ready=1;mod_test_hub_partner_heroine_ready=1;mod_test_hub_partner_sword_attack_speed=1;mod_test_hub_partner_heroine_attack_speed=1;mod_test_hub_partner_sword_x=33;mod_test_hub_partner_sword_y=22;mod_test_hub_partner_heroine_x=35;mod_test_hub_partner_heroine_y=22",
        "interactive hub state assertions",
    )
    assert_contains(
        scenarios_ini,
        "two combat partners reset to distinct adjacent tiles, and two northern enemies; all four combat fixtures use normal attack cadence",
        "interactive hub metadata",
    )


def test_message_and_trilogy_tooltip_layout_contract() -> None:
    ui_root = REPO_ROOT / "assets" / "xjxqy" / "ini" / "ui"

    def read_rect(root: Path, relative_path: str) -> tuple[int, int, int, int]:
        parser = read_ini(root / relative_path)
        return tuple(
            int(get_value(parser, "Init", key))
            for key in ("Left", "Top", "Width", "Height")
        )

    message_window = read_rect(ui_root, "message/window.ini")
    message_label = read_rect(ui_root, "message/label.ini")
    message_window_parser = read_ini(ui_root / "message" / "window.ini")
    message_label_parser = read_ini(ui_root / "message" / "label.ini")
    if int(get_value(message_window_parser, "Init", "AlignY")) != -75:
        raise AssertionError("XJXQY message window must stay 75px above the bottom anchor")
    if int(get_value(message_label_parser, "Init", "Font")) > 12:
        raise AssertionError("XJXQY message label must follow the original 12px font")
    if message_label[0] < 30:
        raise AssertionError("XJXQY message label needs the original 30px left padding")
    if (
        message_label[0] + message_label[2] > message_window[2]
        or message_label[1] + message_label[3] > message_window[3]
    ):
        raise AssertionError("XJXQY message label exceeds its background")

    tooltip_window = read_rect(ui_root, "tooltip/window.ini")
    if tooltip_window[2] != 288:
        raise AssertionError("XJXQY tooltip must follow the original type-2 288px width")
    tooltip_menu = (ui_root / "tooltip" / "tooltip.menu.ini").read_text(
        encoding="utf-8-sig"
    )
    if "name=image" in tooltip_menu:
        raise AssertionError("XJXQY type-2 tooltip must not render the type-1 item image")
    for component_name in ("name", "cost", "intro1", "intro2"):
        component_rect = read_rect(ui_root, f"tooltip/{component_name}.ini")
        if (
            component_rect[0] < 0
            or component_rect[1] < 0
            or component_rect[0] + component_rect[2] > tooltip_window[2]
            or component_rect[1] + component_rect[3] > tooltip_window[3]
        ):
            raise AssertionError(
                f"XJXQY tooltip component exceeds background: {component_name}"
            )

    yycs_root = REPO_ROOT / "assets" / "yycs" / "ini" / "ui"

    yycs_message_window = read_rect(yycs_root, "message/window.ini")
    yycs_message_window_parser = read_ini(
        yycs_root / "message" / "window.ini"
    )
    yycs_bottom_window = read_rect(yycs_root, "bottom/window.ini")
    yycs_message_align_y = int(
        get_value(yycs_message_window_parser, "Init", "AlignY")
    )
    yycs_message_bottom = 480 + yycs_message_align_y
    yycs_bottom_top = 480 - yycs_bottom_window[3]
    if yycs_message_window[3] != 94 or yycs_message_align_y != -71:
        raise AssertionError("YYCS message window must use the raised bottom anchor")
    if yycs_message_bottom >= yycs_bottom_top:
        raise AssertionError("YYCS message window overlaps the bottom menu")

    yycs_window = read_rect(yycs_root, "tooltip/window.ini")
    yycs_window_parser = read_ini(yycs_root / "tooltip" / "window.ini")
    if get_value(yycs_window_parser, "Init", "Align").lower() != "altopcenter":
        raise AssertionError("YYCS type-1 tooltip must remain top-centered")
    if int(get_value(yycs_window_parser, "Init", "AlignY")) != 27:
        raise AssertionError("YYCS type-1 tooltip must retain its original 27px top offset")
    yycs_menu = (yycs_root / "tooltip" / "tooltip.menu.ini").read_text(
        encoding="utf-8-sig"
    )
    if "name=magicIntro" not in yycs_menu:
        raise AssertionError("YYCS type-1 tooltip needs an independent magic intro label")
    expected_yycs_rects = {
        "image": (132, 47, 60, 75),
        "name": (67, 191, 100, 20),
        "cost": (160, 191, 88, 20),
        "intro1": (67, 215, 188, 20),
        "magicintro": (67, 210, 196, 120),
        "intro2": (67, 245, 196, 100),
    }
    for component_name, expected_rect in expected_yycs_rects.items():
        component_rect = read_rect(yycs_root, f"tooltip/{component_name}.ini")
        if component_rect != expected_rect:
            raise AssertionError(
                f"YYCS tooltip layout drift for {component_name}: {component_rect}"
            )
        if (
            component_rect[0] + component_rect[2] > yycs_window[2]
            or component_rect[1] + component_rect[3] > yycs_window[3]
        ):
            raise AssertionError(f"YYCS tooltip component exceeds background: {component_name}")

    for pack_name in ("月眉儿外传1.053", "江湖余尘1.03", "江湖余尘二", "潇湘行1.022"):
        local_tooltip_root = REPO_ROOT / "assets" / pack_name / "ini" / "ui"
        local_window_parser = read_ini(local_tooltip_root / "tooltip" / "window.ini")
        if get_value(local_window_parser, "Init", "Align").lower() != "altopcenter":
            raise AssertionError(f"{pack_name} must retain the inherited YYCS top anchor")
        for component_name in ("intro1", "intro2"):
            component_rect = read_rect(
                local_tooltip_root, f"tooltip/{component_name}.ini"
            )
            if component_rect != expected_yycs_rects[component_name]:
                raise AssertionError(
                    f"{pack_name} shadows the corrected YYCS {component_name} layout: "
                    f"{component_rect}"
                )

    jxqy2_root = REPO_ROOT / "assets" / "jxqy2" / "ini" / "ui"
    jxqy2_window = read_rect(jxqy2_root, "tooltip/window.ini")
    for component_name in ("image", "name", "cost", "intro1", "intro2"):
        component_rect = read_rect(jxqy2_root, f"tooltip/{component_name}.ini")
        if (
            component_rect[0] < 0
            or component_rect[1] < 0
            or component_rect[0] + component_rect[2] > jxqy2_window[2]
            or component_rect[1] + component_rect[3] > jxqy2_window[3]
        ):
            raise AssertionError(
                f"JXQY2 tooltip component exceeds background: {component_name}"
            )
    jxqy2_intro_parser = read_ini(jxqy2_root / "tooltip" / "intro2.ini")
    jxqy2_intro_font = int(get_value(jxqy2_intro_parser, "Init", "Font"))
    if jxqy2_intro_font > 18:
        raise AssertionError("JXQY2 tooltip intro font must fit four description lines")
    longest_jxqy2_intro_length = 0
    for ini_path in (
        list((REPO_ROOT / "assets" / "jxqy2" / "ini" / "goods").glob("*.ini"))
        + list((REPO_ROOT / "assets" / "jxqy2" / "ini" / "magic").glob("*.ini"))
    ):
        parser = read_ini(ini_path)
        intro = get_value(parser, "Init", "Intro", "")
        longest_jxqy2_intro_length = max(longest_jxqy2_intro_length, len(intro))
    intro_rect = read_rect(jxqy2_root, "tooltip/intro2.ini")
    characters_per_line = max(1, intro_rect[2] // jxqy2_intro_font)
    required_line_count = max(
        1,
        (longest_jxqy2_intro_length + characters_per_line - 1)
        // characters_per_line,
    )
    if intro_rect[1] + required_line_count * jxqy2_intro_font > jxqy2_window[3]:
        raise AssertionError("JXQY2 longest tooltip description exceeds its background")


def test_yycs_early_bat_death_script_does_not_run_inn_dialogue() -> None:
    early_bat_script = (
        REPO_ROOT
        / "assets"
        / "yycs"
        / "script"
        / "map"
        / "map_009_山洞内部"
        / "吸血蝙蝠死亡.txt"
    ).read_text(encoding="utf-8-sig")
    normalized_early_script = early_bat_script.lower()
    if "talk(" in normalized_early_script or "say(" in normalized_early_script:
        raise AssertionError("YYCS early bat death script must not run inn dialogue")
    if "return;" not in normalized_early_script:
        raise AssertionError("YYCS early bat death script must remain a valid no-op")

    late_bat_script = (
        REPO_ROOT
        / "assets"
        / "yycs"
        / "script"
        / "map"
        / "map_055_山洞"
        / "吸血蝙蝠死亡.txt"
    ).read_text(encoding="utf-8-sig")
    normalized_late_script = late_bat_script.lower()
    if 'add("deadbat",1);' not in normalized_late_script:
        raise AssertionError("YYCS late bat death counter must remain active")


def test_generated_pack_matches_installed_golden_files() -> None:
    installed_root = REPO_ROOT / "assets" / "xjxqy_test_mod"
    expected_text = dict(scenario_files())
    args = parse_args([])
    args.save_namespace = args.pack_id
    base_pack = discover_base_pack(REPO_ROOT / "assets", args.base_id)
    expected_text["game_profile.ini"] = make_profile(args, base_pack, args.pack_path)
    expected_text["readme.md"] = make_readme(args.pack_id, args.base_id)

    installed_names = {
        path.relative_to(installed_root).as_posix()
        for path in installed_root.rglob("*")
        if path.is_file()
        and path.relative_to(installed_root).parts[0]
        not in {"save", ".mod-scenario-smoke-save-backup"}
        and path.relative_to(installed_root).name != ".mod-scenario-smoke.lock"
    }
    expected_names = set(expected_text)
    if installed_names != expected_names:
        missing = sorted(expected_names - installed_names)
        extra = sorted(installed_names - expected_names)
        raise AssertionError(f"installed test MOD drift: missing={missing}, extra={extra}")

    different = []
    for relative_name, content in expected_text.items():
        if (installed_root / relative_name).read_bytes() != content.encode("utf-8"):
            different.append(relative_name)
    if different:
        raise AssertionError(f"installed test MOD content drift: {different}")

    validation_errors = validate_generated_pack(installed_root, args.pack_id)
    if validation_errors:
        raise AssertionError(f"installed test MOD validation failed: {validation_errors}")


def test_scaffold_paths_reject_absolute_traversal_and_existing_link_escape() -> None:
    for unsafe in ("../outside", "C:/outside", "/outside", "mods/my ; mod"):
        try:
            normalize_pack_path(unsafe)
        except ValueError:
            pass
        else:
            raise AssertionError(f"normalize_pack_path accepted unsafe path {unsafe!r}")

    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = make_minimal_assets(Path(temp_dir))
        nested_args = parse_args(
            [
                "--assets-root", str(assets_root),
                "--install",
                "--pack-path", "mods/nested",
            ]
        )
        try:
            scaffold(nested_args)
        except ValueError:
            pass
        else:
            raise AssertionError("scaffold installed a nested pack the runtime cannot discover")

    with tempfile.TemporaryDirectory() as root_dir, tempfile.TemporaryDirectory() as outside_dir:
        root = Path(root_dir)
        link = root / "link"
        try:
            link.symlink_to(Path(outside_dir), target_is_directory=True)
        except OSError:
            return
        try:
            resolve_contained_path(root, "link/generated.ini", "test output")
        except ValueError:
            pass
        else:
            raise AssertionError("resolve_contained_path accepted an existing link escape")


def test_scaffold_lock_rejects_broken_symlink_without_touching_target() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        outside_target = root / "outside" / "missing.lock"
        lock_path = root / "scaffold.lock"
        try:
            lock_path.symlink_to(outside_target)
        except OSError:
            return
        try:
            with scaffold_module.exclusive_file_lock(lock_path):
                pass
        except ValueError:
            pass
        else:
            raise AssertionError("scaffold followed a broken lock symlink")
        if outside_target.exists():
            raise AssertionError("scaffold created a lock target outside the lock directory")


def test_scaffold_discovery_handles_percent_case_and_duplicate_id() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = Path(temp_dir)
        enabled_profile = assets_root / "enabled" / "game_profile.ini"
        enabled_profile.parent.mkdir(parents=True, exist_ok=True)
        enabled_profile.write_text(
            "[game]\nid=BASE\nname=100% Test\ntype=2\n[title]\nmenu=ini\\ui\\title.menu.ini\n",
            encoding="utf-8",
        )

        base_pack = discover_base_pack(assets_root, "base")
        if base_pack.root != (assets_root / "enabled").resolve():
            raise AssertionError("direct-child profile was not discovered by Game.Id")
        profile = read_ini(enabled_profile)
        if profile.get("game", "name") != "100% Test":
            raise AssertionError("INI reader unexpectedly interpolated a literal percent value")

        duplicate_profile = assets_root / "duplicate" / "game_profile.ini"
        duplicate_profile.parent.mkdir(parents=True)
        duplicate_profile.write_text("[Game]\nId=base\nName=Duplicate\n", encoding="utf-8")
        try:
            discover_base_pack(assets_root, "BASE")
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate direct-child Game.Id was accepted")


def test_runtime_compatible_ini_view_uses_last_case_variant_without_default_inheritance() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        ini_path = Path(temp_dir) / "runtime.ini"
        ini_path.write_text(
            "\n".join(
                [
                    "[DEFAULT]",
                    "Path=inherited",
                    "[Pack.MOD]",
                    "Path=first",
                    "path=second ; runtime inline comment",
                    "[pack.mod]",
                    "PATH=third ; final value",
                    "[Pack.EMPTY]",
                    "Id=EMPTY",
                    "",
                ]
            ),
            encoding="utf-8",
        )
        parser = read_ini(ini_path)
        if get_value(parser, "Pack.MOD", "Path") != "third":
            raise AssertionError("case-insensitive INI view did not keep the runtime last value")
        if get_value(parser, "Pack.EMPTY", "Path", "missing") != "missing":
            raise AssertionError("[DEFAULT] values leaked into runtime-compatible sections")


def test_generated_cleanup_rejects_internal_link_without_deleting_target() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        victim = root / "custom.txt"
        victim.write_text("keep\n", encoding="utf-8")
        try:
            (root / "game_profile.ini").symlink_to(victim)
        except OSError:
            return
        try:
            scaffold_module.clean_generated_pack(root)
        except ValueError:
            pass
        else:
            raise AssertionError("cleanup followed an internal generated-file symlink")
        if victim.read_text(encoding="utf-8") != "keep\n":
            raise AssertionError("cleanup deleted the internal symlink target")


def test_scaffold_rejects_shared_common_root_before_cleaning() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = make_minimal_assets(Path(temp_dir))
        victim = assets_root / "common" / "script" / "common" / "newgame.txt"
        victim.parent.mkdir(parents=True, exist_ok=True)
        victim.write_text("shared sentinel\n", encoding="utf-8")
        args = parse_args(
            [
                "--assets-root", str(assets_root),
                "--install",
                "--pack-id", "MOD",
                "--pack-path", "common",
                "--base-id", "XJXQY",
                "--force",
                "--clean",
            ]
        )
        try:
            scaffold(args)
        except ValueError:
            pass
        else:
            raise AssertionError("scaffold accepted Collection.CommonPath as a MOD output root")
        if victim.read_text(encoding="utf-8") != "shared sentinel\n":
            raise AssertionError("shared common resource was changed before overlap rejection")


def test_scaffold_transaction_rolls_back_pack_on_generation_failure() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = make_minimal_assets(Path(temp_dir))
        mod_root = assets_root / "mod"
        mod_root.mkdir(parents=True, exist_ok=True)
        old_profile = "[Game]\nId=MOD\nName=Old\n[Save]\nNamespace=MOD\n"
        (mod_root / "game_profile.ini").write_text(old_profile, encoding="utf-8")
        (mod_root / "readme.md").write_text("OLD_README\n", encoding="utf-8")
        (mod_root / "custom.txt").write_text("KEEP\n", encoding="utf-8")
        resources_before = (assets_root / "resources.ini").read_bytes()
        args = parse_args(
            [
                "--assets-root", str(assets_root),
                "--install",
                "--pack-id", "MOD",
                "--pack-path", "mod",
                "--base-id", "XJXQY",
                "--save-namespace", "MOD",
                "--force",
            ]
        )

        original_write_text = scaffold_module.write_text
        calls = 0

        def failing_write_text(path, text, force, written):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("injected generated-file write failure")
            return original_write_text(path, text, force, written)

        with patch.object(scaffold_module, "write_text", failing_write_text):
            try:
                scaffold(args)
            except OSError:
                pass
            else:
                raise AssertionError("generation failure injection unexpectedly succeeded")

        if (mod_root / "game_profile.ini").read_text(encoding="utf-8") != old_profile:
            raise AssertionError("generation failure left a partially updated pack")
        if (mod_root / "readme.md").read_text(encoding="utf-8") != "OLD_README\n":
            raise AssertionError("generation failure did not restore the old README")
        if (mod_root / "custom.txt").read_text(encoding="utf-8") != "KEEP\n":
            raise AssertionError("generation failure lost an unknown pack file")
        if (assets_root / "resources.ini").read_bytes() != resources_before:
            raise AssertionError("generation failure changed resources.ini")
        if list(assets_root.glob(".mod.scaffold-*")):
            raise AssertionError("generation rollback left an unnecessary transaction directory")


def test_scaffold_transaction_preserves_unknown_and_save_files_on_success() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = make_minimal_assets(Path(temp_dir))
        mod_root = assets_root / "mod"
        (mod_root / "save" / "rpg0").mkdir(parents=True, exist_ok=True)
        (mod_root / "save" / "rpg0" / "game.ini").write_text("SAVE\n", encoding="utf-8")
        (mod_root / "custom.txt").write_text("KEEP\n", encoding="utf-8")
        args = parse_args(
            [
                "--assets-root", str(assets_root),
                "--install",
                "--pack-id", "MOD",
                "--pack-path", "mod",
                "--base-id", "XJXQY",
                "--save-namespace", "MOD",
                "--force",
            ]
        )
        with redirect_stdout(StringIO()):
            if scaffold(args) != 0:
                raise AssertionError("transactional scaffold success path failed")
        if (mod_root / "custom.txt").read_text(encoding="utf-8") != "KEEP\n":
            raise AssertionError("successful pack swap lost an unknown file")
        if (mod_root / "save" / "rpg0" / "game.ini").read_text(encoding="utf-8") != "SAVE\n":
            raise AssertionError("successful pack swap lost save data")
        if validate_generated_pack(mod_root, "MOD"):
            raise AssertionError("successful staged pack did not validate after publication")


def test_scaffold_transaction_rolls_back_interrupts_after_live_renames() -> None:
    for phase, exception_type in (
        ("old_moved", KeyboardInterrupt),
        ("old_moved", ValueError),
        ("new_published", KeyboardInterrupt),
        ("new_published", ValueError),
    ):
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = make_minimal_assets(Path(temp_dir))
            mod_root = assets_root / "mod"
            mod_root.mkdir(parents=True, exist_ok=True)
            old_profile = "[Game]\nId=MOD\nName=Old\n[Save]\nNamespace=MOD\n"
            (mod_root / "game_profile.ini").write_text(old_profile, encoding="utf-8")
            (mod_root / "custom.txt").write_text("KEEP\n", encoding="utf-8")
            args = parse_args(
                [
                    "--assets-root", str(assets_root),
                    "--install",
                    "--pack-id", "MOD",
                    "--pack-path", "mod",
                    "--base-id", "XJXQY",
                    "--save-namespace", "MOD",
                    "--force",
                ]
            )
            original_journal = scaffold_module.write_transaction_journal

            def interrupting_journal(journal_path, current_phase, output_root, staging_root, backup_root):
                if current_phase == phase:
                    raise exception_type(f"injected {phase} interruption")
                return original_journal(
                    journal_path,
                    current_phase,
                    output_root,
                    staging_root,
                    backup_root,
                )

            with patch.object(scaffold_module, "write_transaction_journal", interrupting_journal):
                try:
                    scaffold(args)
                except (KeyboardInterrupt, ValueError):
                    pass
                else:
                    raise AssertionError(f"{phase} interruption unexpectedly succeeded")

            if (mod_root / "game_profile.ini").read_text(encoding="utf-8") != old_profile:
                raise AssertionError(f"{phase} interruption did not restore the old profile")
            if (mod_root / "custom.txt").read_text(encoding="utf-8") != "KEEP\n":
                raise AssertionError(f"{phase} interruption lost the old pack")
            if list(assets_root.glob(".mod.scaffold-*")):
                raise AssertionError(f"{phase} interruption left a transaction after successful rollback")


def test_scaffold_install_rejects_normalized_save_namespace_collision() -> None:
    for requested, existing in (
        ("XJXQY", "XJXQY"),
        ("xjxqy", "XJXQY"),
        ("SHARED:SAVE", "shared.save"),
        ("??", "!!!"),
    ):
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = make_minimal_assets(Path(temp_dir))
            base_profile = assets_root / "xjxqy" / "game_profile.ini"
            base_profile.write_text(
                base_profile.read_text(encoding="utf-8").replace(
                    "Namespace=XJXQY",
                    f"Namespace={existing}",
                ),
                encoding="utf-8",
            )
            args = parse_args(
                [
                    "--assets-root", str(assets_root),
                    "--install",
                    "--pack-id", "MOD",
                    "--pack-path", "mod",
                    "--base-id", "XJXQY",
                    "--save-namespace", requested,
                    "--force",
                ]
            )
            try:
                scaffold(args)
            except ValueError:
                pass
            else:
                raise AssertionError(f"save namespace collision was accepted: {requested!r}")


def test_discovered_namespace_fallback_uses_manifest_id() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = Path(temp_dir) / "assets"
        assets_root.mkdir(parents=True)
        (assets_root / "resources.ini").write_text(
            "[Collection]\nCommonPath=common\n",
            encoding="utf-8",
        )
        profile = assets_root / "pack" / "game_profile.ini"
        profile.parent.mkdir(parents=True)
        profile.write_text("[Game]\nId=MANIFEST_ID\n", encoding="utf-8")
        namespaces = scaffold_module.discovered_save_namespaces(assets_root)
        if len(namespaces) != 1 or namespaces[0][2] != "manifest_id":
            raise AssertionError(f"namespace fallback did not use manifest Game.Id: {namespaces}")


def test_default_non_install_scaffold_uses_draft_root_without_touching_assets() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        repo_root = Path(temp_dir)
        assets_root = make_minimal_assets(repo_root)
        resources_before = (assets_root / "resources.ini").read_bytes()
        with patch.object(scaffold_module, "REPO_ROOT", repo_root):
            args = scaffold_module.parse_args([])
            with redirect_stdout(StringIO()):
                if scaffold_module.scaffold(args) != 0:
                    raise AssertionError("default non-install scaffold failed")
        draft_root = repo_root / "tmp" / "test_mod_skeleton" / "xjxqy_test_mod"
        if not (draft_root / "game_profile.ini").exists():
            raise AssertionError("default non-install scaffold did not write the draft root")
        if (assets_root / "resources.ini").read_bytes() != resources_before:
            raise AssertionError("default non-install scaffold changed resources.ini")


def test_scaffold_detects_stale_transaction_for_bracketed_output_name() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        assets_root = make_minimal_assets(root)
        output_root = root / "drafts" / "[mod]"
        stale_root = output_root.parent / ".[mod].scaffold-crash"
        stale_root.mkdir(parents=True)
        args = parse_args(
            [
                "--assets-root", str(assets_root),
                "--output-root", str(output_root),
                "--pack-id", "MOD",
                "--pack-path", "mod",
                "--base-id", "XJXQY",
                "--save-namespace", "MOD",
                "--force",
            ]
        )
        try:
            scaffold(args)
        except ValueError:
            pass
        else:
            raise AssertionError("bracketed stale transaction name bypassed recovery refusal")
        if not stale_root.exists() or output_root.exists():
            raise AssertionError("stale transaction refusal changed the recovery artifacts")


def test_scaffold_rejects_inline_comment_in_serialized_scalar() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = make_minimal_assets(Path(temp_dir))
        args = parse_args(
            [
                "--assets-root", str(assets_root),
                "--install",
                "--pack-id", "MOD",
                "--pack-path", "mod",
                "--base-id", "XJXQY",
                "--save-namespace", "XJXQY ; hidden suffix",
                "--force",
            ]
        )
        try:
            scaffold(args)
        except ValueError:
            pass
        else:
            raise AssertionError("INI inline-comment sequence was accepted in Save.Namespace")


def main() -> int:
    test_functions = [
        function
        for name, function in globals().items()
        if name.startswith("test_") and callable(function)
    ]
    if not test_functions:
        raise AssertionError("no scaffold tests were discovered")
    for function in test_functions:
        function()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
