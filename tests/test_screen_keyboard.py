from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class KeyboardBuildGuards(unittest.TestCase):
    def run_build(self, directory):
        return subprocess.run(['bash',str(ROOT/'scripts/build-screen-keyboard.sh')],env={**os.environ,'AGINO_OUTPUT_DIR':str(directory)},capture_output=True,text=True)
    def test_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'components/agino-keyboard';p.mkdir(parents=True);sentinel=p/'existing.deb';sentinel.write_bytes(b'keep')
            r=self.run_build(d);self.assertNotEqual(r.returncode,0);self.assertIn('Build output exists',r.stderr);self.assertEqual(sentinel.read_bytes(),b'keep')
    def test_missing_archive_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as d:
            r=self.run_build(d);self.assertNotEqual(r.returncode,0);self.assertIn('Missing rootfs archive',r.stderr);self.assertFalse((Path(d)/'components').exists())

class KeyboardValidationReport(unittest.TestCase):
    def test_missing_hardware_is_not_passed(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('keyboard_verify',ROOT/'scripts/verify-screen-keyboard.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        report=module.report_for(True,None)
        self.assertEqual(report['hardware']['status'],'unverified')
        self.assertFalse(report['release_ready'])
    def test_full_wayland_session_is_required_for_release(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('keyboard_verify',ROOT/'scripts/verify-screen-keyboard.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        evidence={'device':'fixture', **dict.fromkeys(module.HARDWARE_CHECKS,True)}
        self.assertFalse(module.report_for(True,evidence)['release_ready'])
        evidence['dde_wayland_session']=True
        self.assertFalse(module.report_for(True,evidence)['release_ready'])
        memory={'cold_boots':3,'settle_seconds':120,'samples_per_boot':60,'peaks':[900000000]*3}
        self.assertTrue(module.report_for(True,evidence,memory)['release_ready'])
        memory['peaks'][2]=1000000001
        self.assertFalse(module.report_for(True,evidence,memory)['release_ready'])
        memory['peaks']=[900000000]*3;memory['samples_per_boot']=5
        self.assertFalse(module.report_for(True,evidence,memory)['release_ready'])
    def test_failed_automation_is_not_passed(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('keyboard_verify',ROOT/'scripts/verify-screen-keyboard.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        self.assertFalse(module.report_for(False,None)['automated_passed'])
    def test_shell_scripts_parse(self):
        for filename in ['scripts/build-screen-keyboard.sh','scripts/guest/build-screen-keyboard.sh','apps/agino-keyboard/tests/private-session.sh','apps/agino-keyboard/tests/nested-session.sh','apps/agino-keyboard/data/agino-keyboard-fcitx-wayland','apps/agino-keyboard/data/agino-keyboard-fcitx-x11']:
            result=subprocess.run(['bash','-n',str(ROOT/filename)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
