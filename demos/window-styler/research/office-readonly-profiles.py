"""Exact module triplets for read-only NetUI discovery, never injection profiles.

The updated Node accessors were mapped through the recorded export ordinals
and their complete getter instructions. A matching identity alone is not a
layout or rendering pass, and these profiles do not register any backend.
"""

PROFILES = (
    {
        'name': 'legacy-readonly',
        'modules': {
            'mso40uiwin32client.dll': ('2D264595-7EA8-4318-8AA1-897691F9D8EE', 2),
            'mso98win32client.dll': ('7DA1C564-0BEE-44F1-833B-5036838211D4', 2),
            'mso.dll': ('609D7177-47D3-4508-B59D-AB1FD4FB1624', 2),
        },
        'node_accessors': ((0x1af790, '488b4110c3'), (0x1dc470, '488b4108c3'),
                           (0x1dbc60, '488b4118c3'), (0x1dc4a0, '488b4120c3')),
    },
    {
        'name': '16.0.14334.21048-readonly',
        'modules': {
            'mso40uiwin32client.dll': ('3E4B8C5A-B16A-4DE5-B063-338EDF2BB546', 2),
            'mso98win32client.dll': ('587D5BED-5243-4E42-9E0E-87C87C6A4E20', 2),
            'mso.dll': ('6FF021ED-7C7B-4196-BC82-49F056FF3CF8', 2),
        },
        'node_accessors': ((0x1af9a0, '488b4110c3'), (0x1dc680, '488b4108c3'),
                           (0x1dbe70, '488b4118c3'), (0x1dc6b0, '488b4120c3')),
    },
)


def select_profile(identities):
    """Require one complete known triplet; never mix modules across versions."""
    normalized = {name.lower(): (value[0].upper(), value[1])
                  for name, value in identities.items()}
    if len(normalized) != len(identities):
        raise ValueError('Duplicate Office module identity')
    for profile in PROFILES:
        if normalized == profile['modules']:
            return profile
    raise ValueError('Incomplete, mixed, or unknown Office read-only ABI profile')
