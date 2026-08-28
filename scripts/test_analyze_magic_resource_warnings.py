#!/usr/bin/env python3
"""Focused regression tests for magic resource warning classification."""

from __future__ import annotations

from analyze_magic_resource_warnings import (
    GROUP_ALIAS_FALLBACK,
    GROUP_DIRECT_ACTION_MISSING,
    parse_warning,
)


def main() -> int:
    action_alias = parse_warning(
        {
            "severity": "WARNING",
            "pack_id": "MOD",
            "category": "magic_action_image",
            "message": (
                "magic action resource UseActionFile target resolves to different disk path "
                "(via dependency): asf\\character\\attack.asf -> "
                "assets\\base\\asf\\character\\attack.asf"
            ),
            "path": "assets/mod/ini/magic/test.ini:8",
        }
    )
    assert action_alias["field"] == "UseActionFile"
    assert action_alias["target"] == "asf\\character\\attack.asf"
    assert action_alias["resolved"] == "assets\\base\\asf\\character\\attack.asf"
    assert action_alias["resolution_kind"] == "resolves to different disk path"
    assert action_alias["group"] == GROUP_ALIAS_FALLBACK

    effect_alias = parse_warning(
        {
            "severity": "WARNING",
            "pack_id": "MOD",
            "category": "magic_effect_image",
            "message": (
                "magic effect resource FlyingImage target resolves to different disk path "
                "(via common): asf\\effect\\hit.asf -> common\\asf\\effect\\hit.asf"
            ),
        }
    )
    assert effect_alias["group"] == GROUP_ALIAS_FALLBACK

    direct_missing = parse_warning(
        {
            "severity": "WARNING",
            "pack_id": "MOD",
            "category": "magic_action_image",
            "message": "magic action resource ActionShadowFile target not found: missing.asf",
        }
    )
    assert direct_missing["group"] == GROUP_DIRECT_ACTION_MISSING
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
