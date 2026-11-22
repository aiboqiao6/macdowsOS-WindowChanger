"""Check that read-only discovery never combines partial Office ABI profiles."""
import copy
import pathlib
import runpy
import unittest

profiles = runpy.run_path(str(pathlib.Path(__file__).with_name('office-readonly-profiles.py')))
select_profile = profiles['select_profile']


class OfficeReadOnlyProfiles(unittest.TestCase):
    def test_complete_triplets(self):
        for profile in profiles['PROFILES']:
            with self.subTest(profile=profile['name']):
                self.assertEqual(select_profile(profile['modules'])['name'], profile['name'])

    def test_missing_modules(self):
        for profile in profiles['PROFILES']:
            for name in profile['modules']:
                with self.subTest(profile=profile['name'], missing=name):
                    partial = dict(profile['modules'])
                    del partial[name]
                    with self.assertRaises(ValueError):
                        select_profile(partial)

    def test_mixed_versions(self):
        old, new = (profile['modules'] for profile in profiles['PROFILES'])
        for name in old:
            with self.subTest(swapped=name):
                mixed = dict(old)
                mixed[name] = new[name]
                with self.assertRaises(ValueError):
                    select_profile(mixed)

    def test_age_is_part_of_identity(self):
        for profile in profiles['PROFILES']:
            for name, (guid, age) in profile['modules'].items():
                with self.subTest(profile=profile['name'], changed=name):
                    stale = dict(profile['modules'])
                    stale[name] = guid, age + 1
                    with self.assertRaises(ValueError):
                        select_profile(stale)

    def test_unknown_build_is_not_a_version_prefix_match(self):
        for profile in profiles['PROFILES']:
            changed = copy.deepcopy(profile['modules'])
            changed['mso.dll'] = ('00000000-0000-0000-0000-000000000000', 2)
            with self.assertRaises(ValueError):
                select_profile(changed)

    def test_case_normalization_does_not_hide_duplicates(self):
        original = profiles['PROFILES'][1]['modules']
        case_only = {name.upper(): (guid.lower(), age) for name, (guid, age) in original.items()}
        self.assertEqual(select_profile(case_only)['name'], profiles['PROFILES'][1]['name'])
        duplicate = dict(original)
        duplicate['MSO.DLL'] = original['mso.dll']
        with self.assertRaises(ValueError):
            select_profile(duplicate)


if __name__ == '__main__':
    unittest.main()
