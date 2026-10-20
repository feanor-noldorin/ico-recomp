"""Smoke tests for the install pipeline: no game files, no Windows, no network.

Run from the repository root:  python -m unittest discover -s tests -v
They cover what can break before a player ever
reaches the compile step: the profile, the pipeline's argument and input
checks, the pinned dependencies, and the release script.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent.parent
PIPELINE = ROOT / "pipeline"
sys.path.insert(0, str(PIPELINE))

import ico_pipeline as ip  # noqa: E402

HEX64 = re.compile(r"^[0-9a-f]{64}$")
HEX_ADDR = re.compile(r"^0x[0-9A-Fa-f]+$")


def load_profile() -> dict:
    return json.loads((PIPELINE / "ico_profile.json").read_text(encoding="utf-8"))


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.p = load_profile()

    def test_hashes_are_sha256_hex(self):
        g = self.p["game_files"]
        hashes = [g["self_sha256"], g["elf_sha256"]]
        hashes += [i["sha256"] for i in self.p["spu_images"]]
        hashes += [o["sha256"] for o in self.p["spu_overlays"] if o.get("sha256")]
        hashes.append(self.p["sdl2"]["sha256"])
        for h in hashes:
            self.assertRegex(h, HEX64)

    def test_code_ends_are_hex_or_null(self):
        ends = [self.p["ppu"]["code_end"]]
        ends += [i["code_end"] for i in self.p["spu_images"]]
        ends += [o["code_end"] for o in self.p["spu_overlays"]]
        for e in ends:
            self.assertTrue(e is None or HEX_ADDR.match(e), e)
        self.assertRegex(self.p["ppu"]["code_end"], HEX_ADDR)

    def test_spu_image_names_unique_and_match_their_address(self):
        names = [i["name"] for i in self.p["spu_images"]]
        self.assertEqual(len(names), len(set(names)))
        for n in names:
            self.assertRegex(n, r"^spu_\d{4}_at_[0-9A-F]{8}$")

    def test_overlay_sources_resolve(self):
        names = {i["name"] for i in self.p["spu_images"]}
        dirs = set()
        for o in self.p["spu_overlays"]:
            kind, ref = o["source"].split(":", 1)
            self.assertIn(kind, ("spu_image", "dev_flash"))
            if kind == "spu_image":
                self.assertIn(ref, names)
            self.assertRegex(o["rebase"], HEX_ADDR)
            self.assertNotIn(o["dir"], dirs)
            dirs.add(o["dir"])
            self.assertTrue(o["prefix"].endswith("_"))

    def test_required_files_are_copied(self):
        g = self.p["game_files"]
        for rel in g["required"]:
            self.assertTrue(any(rel == c or rel.startswith(c.rstrip("/") + "/") for c in g["copy"]), rel)

    def test_sdl2_entry_is_consistent(self):
        s = self.p["sdl2"]
        self.assertTrue(s["url"].startswith("https://"))
        self.assertTrue(s["url"].endswith("/" + s["file"]))
        self.assertEqual(s["file"], s["dir"].replace("SDL2-", "SDL2-devel-", 1) + "-VC.zip")

    def test_runtime_env_values_are_strings(self):
        for k, v in self.p["runtime_env"].items():
            self.assertRegex(k, r"^[A-Z0-9_]+$")
            self.assertIsInstance(v, str)


class ConstraintsTests(unittest.TestCase):
    def pins(self) -> dict:
        out = {}
        for line in (PIPELINE / "constraints.txt").read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.fullmatch(r"([A-Za-z0-9_.-]+)==(\d+(?:\.\d+)*)(\s*;\s*python_version\s*[<>=!]+\s*\"[\d.]+\")?", line)
            self.assertIsNotNone(m, f"not an exact pin: {line!r}")
            out[m.group(1).lower()] = m.group(2)
        return out

    def test_every_line_is_an_exact_pin(self):
        self.assertTrue(self.pins())

    def test_covers_the_recompiler_requirements(self):
        req = ROOT / "external" / "ps3recomp" / "requirements.txt"
        if not req.exists():
            self.skipTest("submodule not checked out")
        pins = self.pins()
        for line in req.read_text(encoding="utf-8").splitlines():
            m = re.match(r"([A-Za-z0-9_.-]+)\s*[<>=!~]", line.split(";")[0].strip())
            if m:                                       # conditional ones (tomli) too
                self.assertIn(m.group(1).lower(), pins, f"{m.group(1)} is not pinned")


class CommandLineTests(unittest.TestCase):
    def run_pipeline(self, *args):
        return subprocess.run([sys.executable, "-I", str(PIPELINE / "ico_pipeline.py"), *args],
                              capture_output=True, text=True, timeout=60)

    def test_help(self):
        r = self.run_pipeline("--help")
        self.assertEqual(r.returncode, 0)
        for flag in ("--game-dir", "--elf", "--rpcs3", "--dev-flash", "--install", "--link-mode", "--simd"):
            self.assertIn(flag, r.stdout)

    def test_missing_arguments_are_rejected(self):
        self.assertEqual(self.run_pipeline().returncode, 2)

    def test_elf_and_rpcs3_are_exclusive(self):
        r = self.run_pipeline("--game-dir", "g", "--dev-flash", "d", "--install", "i",
                              "--elf", "e.elf", "--rpcs3", "rpcs3.exe")
        self.assertEqual(r.returncode, 2)
        self.assertIn("not allowed with", r.stderr)

    def test_bad_ps3recomp_reports_an_installer_visible_error(self):
        with tempfile.TemporaryDirectory() as t:
            r = self.run_pipeline("--game-dir", t, "--dev-flash", t, "--install", t + "/i",
                                  "--elf", t + "/e.elf", "--ps3recomp", t)
        self.assertEqual(r.returncode, 1)
        self.assertRegex(r.stdout, r"(?m)^@@ERROR .*ps3recomp was not found")

    def test_step_markers_match_the_step_list(self):
        src = (PIPELINE / "ico_pipeline.py").read_text(encoding="utf-8")
        used = {int(n) for n in re.findall(r"\bstep\((\d+)\)", src)}
        self.assertEqual(used, set(range(1, len(ip.STEPS) + 1)))


class InputCheckTests(unittest.TestCase):
    """check_inputs() against a fake dump."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.t = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        self.prof = load_profile()
        self.game = self.t / "dump" / "PS3_GAME"
        for rel in self.prof["game_files"]["required"]:
            f = self.game / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(b"x")
        self.elf = self.t / "ICO.elf"
        self.elf.write_bytes(b"not the real elf")
        self.fw = self.t / "dev_flash"
        self.fw.mkdir()

    def args(self, **kw):
        base = dict(game_dir=self.t / "dump", elf=self.elf, rpcs3=None, dev_flash=self.fw,
                    work=self.t / "work", allow_unknown=False)
        base.update(kw)
        return SimpleNamespace(**base)

    def test_dump_root_is_resolved_to_ps3_game(self):
        a = self.args()
        with self.assertRaises(ip.PipelineError):    # hash mismatch, after the folder check
            ip.check_inputs(a, self.prof)
        self.assertEqual(a.game_dir, self.game)

    def test_missing_required_file(self):
        (self.game / "PARAM.SFO").unlink()
        with self.assertRaisesRegex(ip.PipelineError, "PARAM.SFO"):
            ip.check_inputs(self.args(), self.prof)

    def test_wrong_elf_is_refused_with_both_hashes(self):
        with self.assertRaisesRegex(ip.PipelineError, "BCUS98259") as cm:
            ip.check_inputs(self.args(), self.prof)
        self.assertIn(hashlib.sha256(self.elf.read_bytes()).hexdigest(), str(cm.exception))

    def test_no_elf_and_no_rpcs3(self):
        with self.assertRaisesRegex(ip.PipelineError, "RPCS3"):
            ip.check_inputs(self.args(elf=None), self.prof)

    def test_missing_firmware_file(self):
        self.prof["game_files"]["elf_sha256"] = hashlib.sha256(self.elf.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ip.PipelineError, "Install the PS3 firmware"):
            ip.check_inputs(self.args(), self.prof)

    def test_good_inputs_pass_and_dev_flash_root_is_resolved(self):
        self.prof["game_files"]["elf_sha256"] = hashlib.sha256(self.elf.read_bytes()).hexdigest()
        for ov in self.prof["spu_overlays"]:
            if ov["source"].startswith("dev_flash:"):
                p = self.fw / "dev_flash" / ov["source"].split(":", 1)[1]
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(b"fw")
        a = self.args(dev_flash=self.fw)           # the RPCS3 folder that contains dev_flash/
        out = ip.check_inputs(a, self.prof)
        self.assertEqual(out, self.t / "work" / "input" / "ICO.elf")
        self.assertEqual(a.dev_flash, self.fw / "dev_flash")


class InstallTests(unittest.TestCase):
    def test_install_writes_ini_and_keeps_user_edits(self):
        prof = load_profile()
        with tempfile.TemporaryDirectory() as td:
            t = Path(td)
            exe, elf = t / "x.exe", t / "ICO.elf"
            exe.write_bytes(b"exe")
            elf.write_bytes(b"elf")
            a = SimpleNamespace(install=t / "inst", dev_flash=Path("/fw/dev_flash"), game_dir=Path("/g"),
                                version="t", link_mode="copy", simd="avx2")
            ip.install(a, prof, exe, elf)
            ini = a.install / "ico.ini"
            self.assertIn("ICO_60FPS=1", ini.read_text())
            self.assertTrue((a.install / "ico.exe").exists())
            self.assertTrue((a.install / "data" / "ICO.elf").exists())
            ini.write_text(ini.read_text().replace("ICO_60FPS=1", "ICO_60FPS=0")
                           .replace("PS3_VFS_ROOT=", "PS3_VFS_ROOT=stale"), encoding="utf-8")
            ip.install(a, prof, exe, elf)
            text = ini.read_text()
            self.assertIn("ICO_60FPS=0", text)                      # a user edit survives an update
            self.assertNotIn("PS3_VFS_ROOT=stale", text)            # the managed paths do not
            self.assertEqual(json.loads((a.install / "install.json").read_text())["version"], "t")

    def test_update_applies_changed_profile_defaults(self):
        """An untouched key takes the new default on update; an edited one stays."""
        prof = load_profile()
        with tempfile.TemporaryDirectory() as td:
            t = Path(td)
            exe, elf = t / "x.exe", t / "ICO.elf"
            exe.write_bytes(b"exe")
            elf.write_bytes(b"elf")
            a = SimpleNamespace(install=t / "inst", dev_flash=Path("/fw"), game_dir=Path("/g"),
                                version="t", link_mode="copy", simd="avx2")
            prof["runtime_env"]["GCM_PREPAREFLIP_QUEUE"] = "0"
            ip.install(a, prof, exe, elf)
            ini = a.install / "ico.ini"
            ini.write_text(ini.read_text().replace("ICO_60FPS=1", "ICO_60FPS=0"), encoding="utf-8")
            prof["runtime_env"]["GCM_PREPAREFLIP_QUEUE"] = "1"      # the next release changes the default
            ip.install(a, prof, exe, elf)
            text = ini.read_text()
            self.assertIn("GCM_PREPAREFLIP_QUEUE=1", text)          # untouched: new default
            self.assertIn("ICO_60FPS=0", text)                      # edited by the user: kept
            prof["runtime_env"]["ICO_60FPS"] = "1"
            ip.install(a, prof, exe, elf)
            self.assertIn("ICO_60FPS=0", ini.read_text())           # still the user's


class IconFallbackTests(unittest.TestCase):
    def test_missing_icon_png_strips_the_icon_from_app_rc(self):
        prof = load_profile()
        with tempfile.TemporaryDirectory() as td:
            t = Path(td)
            port = t / "port"
            port.mkdir()
            (port / "app.rc").write_text((ROOT / "port" / "app.rc").read_text(encoding="utf-8"), encoding="utf-8")
            a = SimpleNamespace(game_dir=t / "PS3_GAME")
            (t / "PS3_GAME").mkdir()
            ip.make_icon(a, prof, port)
            rc = (port / "app.rc").read_text(encoding="utf-8")
            self.assertNotIn(" ICON ", rc)
            self.assertIn("VS_VERSION_INFO", rc)


class RepositoryHygieneTests(unittest.TestCase):
    FORBIDDEN = {".self", ".elf", ".sprx", ".prx", ".psarc", ".pkg", ".iso", ".pic", ".pup", ".at3", ".sfo", ".rap"}

    def test_no_game_or_firmware_files_in_the_tree(self):
        bad = []
        for p in ROOT.rglob("*"):
            rel = p.relative_to(ROOT)
            if p.is_file() and not (set(rel.parts) & {".git", "build", "result", "bin", "obj"}):
                if p.suffix.lower() in self.FORBIDDEN or p.name == "EBOOT.BIN":
                    bad.append(str(rel))
        self.assertEqual(bad, [])

    def test_release_script_writes_checksums_with_lf(self):
        s = (ROOT / "tools" / "make_release.ps1").read_text(encoding="utf-8")
        m = re.search(r"WriteAllText\(.*SHA256SUMS\.txt.*?\"(.*?)\"\)", s)
        self.assertIsNotNone(m, "SHA256SUMS.txt is no longer written with WriteAllText")
        self.assertTrue(m.group(1).endswith("`n"))
        self.assertNotIn("`r", m.group(1))

    def test_installer_passes_the_constraints_file(self):
        s = (ROOT / "installer" / "ICOSetup" / "MainForm.cs").read_text(encoding="utf-8")
        self.assertIn("constraints.txt", s)
        self.assertIn('"-c"', s)

    def test_versions_agree(self):
        """ico.exe, its CMake project and the installer carry one version."""
        csproj = (ROOT / "installer" / "ICOSetup" / "ICOSetup.csproj").read_text(encoding="utf-8")
        v = re.search(r"<Version>(\d+\.\d+\.\d+)</Version>", csproj).group(1)
        rc = (ROOT / "port" / "app.rc").read_text(encoding="utf-8")
        cmake = (ROOT / "port" / "CMakeLists.txt").read_text(encoding="utf-8")
        manifest = (ROOT / "installer" / "ICOSetup" / "app.manifest").read_text(encoding="utf-8")
        self.assertIn(f'"FileVersion", "{v}"', rc)
        self.assertIn(f'"ProductVersion", "{v}"', rc)
        self.assertIn(f"FILEVERSION {v.replace('.', ',')},0", rc)
        self.assertRegex(cmake, rf"\bVERSION {re.escape(v)}\b")
        self.assertIn(f'assemblyIdentity version="{v}.0"', manifest)

    def test_python_sources_compile(self):
        for f in list(PIPELINE.glob("*.py")) + list((ROOT / "tools").glob("*.py")):
            compile(f.read_text(encoding="utf-8"), str(f), "exec")


if __name__ == "__main__":
    unittest.main()
