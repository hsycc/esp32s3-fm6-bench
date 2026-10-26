import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from check_fm6_pcm_log import EXPECTED_TESTS, validate


def fixture():
    return "\n".join(
        [f"FM6_PCM_TEST name={name} status=PASS" for name in sorted(EXPECTED_TESTS)] + [
            "FM6_PCM_OVERLOAD positive=100 negative=101 observed=201 frames=2048",
            "FM6_PCM_SEQUENCE: PASS blocks=1025 passes=2 reference=1 frames=32800 commands=200 routed=200 rejected=0 max_notes=12 checksum=0x12345678 energy=999 mix_clips=0 pcm_clips=0 final_silent=1",
            "QEMU_FM6_PCM: PASS tests=26 failures=0 voices=12 block_frames=32 headroom=4",
            "QEMU_FM6_PCM_GATE: PASS failures=0",
        ]
    ) + "\n"


class PCMLogCheckerTests(unittest.TestCase):
    def test_complete_evidence(self):
        self.assertEqual(validate(fixture())["frames"], "32800")

    def test_missing_directed_case(self):
        with self.assertRaises(ValueError):
            validate(fixture().split("\n", 1)[1])

    def test_duplicate_case(self):
        with self.assertRaises(ValueError):
            validate(fixture() + fixture().splitlines()[0] + "\n")

    def test_failed_then_passed_case_not_hidden(self):
        with self.assertRaises(ValueError):
            validate(fixture().replace("status=PASS", "status=FAIL", 1))

    def test_wrong_accounting(self):
        for old, new in (("routed=200", "routed=199"), ("frames=32800", "frames=32768"),
                         ("observed=201", "observed=200"), ("positive=100", "positive=0"),
                         ("final_silent=1", "final_silent=0"), ("rejected=0", "rejected=1"),
                         ("checksum=0x12345678", "checksum=0x00000000"),
                         ("max_notes=12", "max_notes=11"), ("tests=26", "tests=25")):
            with self.subTest(new=new), self.assertRaises(ValueError):
                validate(fixture().replace(old, new))

    def test_duplicate_summary(self):
        with self.assertRaises(ValueError):
            validate(fixture() + "QEMU_FM6_PCM: PASS tests=26 failures=0 voices=12 block_frames=32 headroom=4\n")

    def test_missing_gate(self):
        with self.assertRaises(ValueError):
            validate(fixture().replace("QEMU_FM6_PCM_GATE: PASS failures=0\n", ""))

    def test_panic_after_pass(self):
        with self.assertRaises(ValueError):
            validate(fixture() + "Guru Meditation Error\n")


if __name__ == "__main__":
    unittest.main()
