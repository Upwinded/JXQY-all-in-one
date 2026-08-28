#!/usr/bin/env python3
"""Focused regression tests for analyze_reference_sources parsers."""

from __future__ import annotations

from analyze_reference_sources import parse_enum_members


def main() -> int:
    enum_body = (
        """
        Decimal = 4,
        Hex = 0xFF, // hexadecimal value
        Alias = Decimal,
        QualifiedAlias = Example::Hex,
        Unknown = 1 << 4,
        ImplicitAfterUnknown,
        Reset = -2,
        ImplicitAfterReset,
        """
    )
    members = parse_enum_members(enum_body.replace("\n", "\r\n"))
    assert [(member["name"], member["value"]) for member in members] == [
        ("Decimal", 4),
        ("Hex", 255),
        ("Alias", 4),
        ("QualifiedAlias", 255),
        ("Unknown", None),
        ("ImplicitAfterUnknown", None),
        ("Reset", -2),
        ("ImplicitAfterReset", -1),
    ]
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
