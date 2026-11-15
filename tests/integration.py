#!/usr/bin/env python3
"""Run real folder -> package -> installation -> upgrade -> uninstall tests."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
PARSER=argparse.ArgumentParser()
PARSER.add_argument("--build-dir",type=Path,default=ROOT/"build")
PARSER.add_argument("--windows",action="store_true",help="Also run Windows artifacts through WSL interop")
ARGS,UNIT_ARGS=PARSER.parse_known_args()
BUILD=ARGS.build_dir.resolve()
APACK=BUILD/("apack.exe" if os.name=="nt" else "apack")

def execute(command,expected=0,env=None):
    result=subprocess.run([str(x) for x in command],capture_output=True,text=True,encoding="utf-8",errors="replace",env=env,timeout=90)
    if expected is not None and result.returncode!=expected:
        raise AssertionError(f"Command failed ({result.returncode}, expected {expected}): {command}\n{result.stdout}\n{result.stderr}")
    return result

def native_windows(path):
    if os.name=="nt":return str(path)
    return execute(["wslpath","-w",path]).stdout.strip()

@unittest.skipUnless(sys.platform.startswith("linux"), "Linux native installers require Linux")
class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix="accretion-test-",dir=BUILD)
        self.root=Path(self.temporary.name)
        self.payload=self.root/"payload"
        (self.payload/"bin").mkdir(parents=True)
        self.output=self.root/"launch-result.txt"
        for name,label in [("main.sh","MAIN"),("cli.sh","CLI")]:
            program=self.payload/"bin"/name
            program.write_text('#!/bin/sh\nprintf "%s\\n" "'+label+'|${APP_DATA-unset}|${1-unset}|${2-unset}" > "$APP_TEST_OUTPUT"\n')
            program.chmod(0o755)
        (self.payload/"data.bin").write_bytes(b"abc\x00xyz"*100000+bytes(range(256)))
        (self.payload/"obsolete.txt").write_text("old-owned-file")
        (self.payload/"config.json").write_text('{"initial":true}')
        self.extra=self.root/"extra";self.extra.mkdir();(self.extra/"plugin.txt").write_text("plugin")
        self.license=self.root/"LICENSE.txt";self.license.write_text("Test license — explicit acceptance is required.\n")
        self.destination=self.root/"install space 应用"
        self.state=self.root/"isolated integration"
        self.project=self.root/"project.json"
        self.manifest={"schemaVersion":1,"app":{"id":"test.accretion.integration","name":"测试应用","version":"1.0.0","publisher":"Tests"},"inputs":{"linux-x64":str(self.payload)},"applications":{"main":{"linux":"bin/main.sh","arguments":["configured"]},"cli":{"linux":"bin/cli.sh","arguments":["cli-configured"]}},"ui":{"modes":["gui","tui"],"default":"tui"},"install":{"scope":"user","allowPathChange":True,"allowScopeChange":True},"files":{"preserve":["config.json"]},"branding":{"licenses":{"zh-CN":str(self.license)}},"components":[{"id":"extra","name":"Optional plugin","defaultChecked":False,"inputs":{"linux-x64":str(self.extra)},"destination":"plugins"}],"environment":[{"id":"home","name":"APP_HOME","value":"${install.dir}","operation":"set","defaultChecked":True},{"id":"data","name":"APP_DATA","value":"${install.dir}/data","operation":"setIfMissing","defaultChecked":True},{"id":"path","name":"AP_TEST_PATH","value":"${install.dir}/bin","operation":"appendUnique","defaultChecked":True},{"id":"output","name":"APP_TEST_OUTPUT","value":str(self.output),"operation":"set","scope":"application","defaultChecked":True}],"shortcuts":[{"id":"desktop","name":"测试工具","location":"desktop","application":"cli","defaultChecked":True},{"id":"menu","name":"测试菜单","location":"menu","application":"main","defaultChecked":True}]}
        self.save()
    def tearDown(self):
        self.temporary.cleanup()
    def save(self):self.project.write_text(json.dumps(self.manifest,ensure_ascii=False),encoding="utf-8")
    def build(self,name="setup.run",mode="tui",archive=False):
        output=self.root/name
        command=[APACK,"build","--project",self.project,"--target","linux-x64","--ui",mode,"--output",output]
        if archive:command += ["--format","archive"]
        execute(command);return output
    def install(self,setup,extra=(),expected=0):
        return execute([setup,"--silent","--accept-license","--dir",self.destination,"--state-root",self.state,*extra],expected=expected)
    def receipt(self):return json.loads((self.destination/".accretion/receipt.json").read_text())
    def test_roundtrip_options_launch_upgrade_uninstall(self):
        setup=self.build()
        inspected=json.loads(execute([APACK,"inspect","--package",setup]).stdout)
        indexed={f["path"]:f for f in inspected["files"]}
        self.assertEqual(indexed["data.bin"]["sha256"],hashlib.sha256((self.payload/"data.bin").read_bytes()).hexdigest())
        self.assertGreater(len(indexed["data.bin"]["chunks"]),1)
        self.assertIn("lz4",{chunk["codec"] for chunk in indexed["data.bin"]["chunks"]})
        self.install(setup,["--deselect","environment:home","--deselect","shortcut:desktop","--select","component:extra"])
        self.assertEqual((self.destination/"data.bin").read_bytes(),(self.payload/"data.bin").read_bytes())
        self.assertTrue((self.destination/"plugins/plugin.txt").is_file())
        self.assertTrue(os.access(self.destination/"bin/main.sh",os.X_OK))
        self.assertFalse((self.state/"desktop/测试工具.desktop").exists())
        menu=self.state/"menu/测试菜单.desktop"
        self.assertIn("--application",menu.read_text())
        self.assertIn("Name=测试菜单",menu.read_text())
        shell=self.state/"environment/user.sh"
        self.assertNotIn("APP_HOME",shell.read_text())
        self.assertIn("AP_TEST_PATH",shell.read_text())
        execute(["/bin/sh","-n",shell])
        actual=execute(["/bin/sh","-c",'. "$1"; printf "%s" "$AP_TEST_PATH"',"sh",shell],env={**os.environ,"AP_TEST_PATH":"original"}).stdout
        self.assertEqual(actual,"original:"+str(self.destination/"bin"))
        helper=self.destination/".accretion/launch"
        execute([helper,"--application","cli","--","extra-argument"])
        deadline=time.monotonic()+5
        while not self.output.exists() and time.monotonic()<deadline:time.sleep(.02)
        self.assertEqual(self.output.read_text().strip(),"CLI|"+str(self.destination/"data")+"|cli-configured|extra-argument")
        (self.destination/"config.json").write_text("user-customized-config")
        (self.destination/"user-created.txt").write_text("keep user data")
        profile=self.state/"profiles/.profile";profile.write_text(profile.read_text()+"# unrelated user edit\n")
        (self.payload/"obsolete.txt").unlink();(self.payload/"new.txt").write_text("new version")
        self.manifest["app"]["version"]="2.0.0";self.save()
        updated=self.build("upgrade.run")
        self.install(updated)
        self.assertEqual(self.receipt()["manifest"]["app"]["version"],"2.0.0")
        self.assertFalse((self.destination/"obsolete.txt").exists())
        self.assertEqual((self.destination/"config.json").read_text(),"user-customized-config")
        self.assertFalse(self.receipt()["selected"]["environment:home"])
        self.assertTrue((self.destination/"plugins/plugin.txt").exists())
        self.assertIn("# unrelated user edit",profile.read_text())
        execute([self.destination/".accretion/uninstall","--silent"])
        self.assertFalse((self.destination/"bin/main.sh").exists())
        self.assertFalse((self.destination/".accretion").exists())
        self.assertFalse(menu.exists());self.assertFalse(shell.exists())
        self.assertEqual((self.destination/"config.json").read_text(),"user-customized-config")
        self.assertEqual((self.destination/"user-created.txt").read_text(),"keep user data")
        self.assertEqual(profile.read_text(),"# unrelated user edit\n")
    def test_license_dry_run_and_collision(self):
        setup=self.build()
        result=execute([setup,"--silent","--dir",self.destination,"--state-root",self.state],expected=1)
        self.assertIn("license",result.stdout.lower());self.assertFalse(self.destination.exists())
        result=execute([setup,"--dry-run","--dir",self.destination,"--state-root",self.state])
        plan=json.loads(result.stdout)
        self.assertEqual(plan["directory"],str(self.destination));self.assertFalse(self.destination.exists());self.assertFalse(self.state.exists())
        self.destination.mkdir();(self.destination/"data.bin").write_text("unrelated original")
        self.install(setup,expected=1)
        self.assertEqual((self.destination/"data.bin").read_text(),"unrelated original")
        self.assertFalse((self.destination/"bin").exists())
        self.assertFalse(Path(str(self.destination)+".accretion-txn").exists())
    def test_corrupted_block_rolls_back(self):
        archive=self.build("payload.acpkg",archive=True)
        data=bytearray(archive.read_bytes());manifest_length=struct.unpack_from("<Q",data,16)[0]
        manifest=json.loads(data[64:64+manifest_length]);last=next(file for file in reversed(manifest["files"]) if file["chunks"] and not file.get("component"))
        chunk=last["chunks"][-1];data[64+manifest_length+chunk["offset"]+chunk["stored"]-1]^=0x7f;archive.write_bytes(data)
        runtime=BUILD/"ap-stubs/ap-setup-linux-x64-tui"
        execute([runtime,"--package",archive,"--silent","--accept-license","--dir",self.destination,"--state-root",self.state],expected=1)
        self.assertFalse(self.destination.exists());self.assertFalse(self.state.exists())
        self.assertFalse(Path(str(self.destination)+".accretion-txn").exists())
    def test_upgrade_registration_failure_restores_previous_version(self):
        setup=self.build();self.install(setup)
        previous=(self.destination/".accretion/receipt.json").read_bytes()
        program=(self.destination/"bin/main.sh").read_bytes()
        (self.payload/"bin/main.sh").write_text("#!/bin/sh\necho updated\n")
        self.manifest["app"]["version"]="2.0.0";self.save();upgrade=self.build("upgrade.run")
        # An unavailable integration destination fails after file replacement.
        shutil.rmtree(self.state/"desktop");(self.state/"desktop").write_text("blocked destination")
        self.install(upgrade,expected=1)
        self.assertEqual((self.destination/"bin/main.sh").read_bytes(),program)
        self.assertEqual((self.destination/".accretion/receipt.json").read_bytes(),previous)
        self.assertEqual((self.state/"desktop").read_text(),"blocked destination")
    def test_traversal_and_external_symlink_rejected(self):
        archive=self.build("payload.acpkg",archive=True);data=archive.read_bytes();length=struct.unpack_from("<Q",data,16)[0]
        manifest=json.loads(data[64:64+length]);manifest["files"][0]["path"]="../escaped-file"
        encoded=json.dumps(manifest,separators=(",",":"),ensure_ascii=False).encode()
        payload=data[64+length:];header=b"ACPKG001"+struct.pack("<IIQQ",1,64,len(encoded),len(payload))+hashlib.sha256(encoded).digest()
        bad=self.root/"traversal.acpkg";bad.write_bytes(header+encoded+payload)
        execute([BUILD/"ap-stubs/ap-setup-linux-x64-tui","--package",bad,"--silent","--accept-license","--dir",self.destination,"--state-root",self.state],expected=1)
        self.assertFalse((self.root/"escaped-file").exists());self.assertFalse(self.destination.exists())
        outside=self.root/"outside.txt";outside.write_text("private")
        (self.payload/"escape.txt").symlink_to(outside)
        execute([APACK,"build","--project",self.project,"--format","archive","--output",self.root/"unsafe.acpkg"],expected=1)
    def test_gui_native_initialization(self):
        if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):self.skipTest("No native graphical display")
        setup=self.build("gui.run",mode="gui")
        gui_env=dict(os.environ)
        # Keep installer leak checking enabled; GTK/fontconfig retain global caches.
        if "suppressions=" not in gui_env.get("LSAN_OPTIONS",""):
            gui_env["LSAN_OPTIONS"]=(gui_env.get("LSAN_OPTIONS","")+":suppressions="+str(ROOT/"tests/lsan.supp")).lstrip(":")
        result=execute([setup,"--gui-smoke-test"],expected=None,env=gui_env)
        if result.returncode and ("display" in result.stdout.lower() or "gtk" in result.stdout.lower()):self.skipTest(result.stdout.strip())
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertFalse(self.destination.exists())

    def test_tui_checkbox_flow(self):
        setup=self.build()
        result=subprocess.run([str(setup),"--ui","tui","--dir",str(self.destination),"--state-root",str(self.state)],
                              input="yes\n\n\n2\n4\n\n",text=True,encoding="utf-8",capture_output=True,timeout=90)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("[x]",result.stdout)
        self.assertFalse(self.receipt()["selected"]["shortcut:desktop"])
        self.assertFalse(self.receipt()["selected"]["environment:home"])

    def test_cross_volume_upgrade_rollback(self):
        with tempfile.TemporaryDirectory(prefix="accretion-cross-volume-",dir="/tmp") as temporary:
            if os.stat(temporary).st_dev==os.stat(self.root).st_dev:
                self.skipTest("A second writable filesystem is unavailable")
            self.state=Path(temporary)/"integrations"
            self.test_upgrade_registration_failure_restores_previous_version()

    def test_mime_registration_and_removal(self):
        self.manifest["associations"]=[{"id":"document","application":"cli","description":"测试文档 & data", "extensions":[".apdemo"]}]
        self.save();self.install(self.build())
        definition=self.state/"mime/packages/test.accretion.integration.document.xml"
        import xml.etree.ElementTree as ET
        root=ET.fromstring(definition.read_text())
        self.assertEqual(root[0].attrib["type"],"application/x-test.accretion.integration.document")
        self.assertEqual(root[0][0].text,"测试文档 & data")
        desktop=self.state/"menu/test.accretion.integration.document.desktop"
        self.assertIn(' %f\n',desktop.read_text())
        self.assertIn('MimeType=application/x-test.accretion.integration.document;',desktop.read_text())
        execute([self.destination/".accretion/uninstall","--silent"])
        self.assertFalse(definition.exists());self.assertFalse(desktop.exists())

    def test_stale_transaction_recovery(self):
        setup=self.build();self.install(setup)
        work=Path(str(self.destination)+".accretion-txn")
        (work/"backup").mkdir(parents=True);(work/"stage").mkdir()
        original=(self.destination/"data.bin").read_bytes()
        backup=work/"backup/0";backup.write_bytes(original)
        (self.destination/"data.bin").write_text("interrupted replacement")
        journal={"schemaVersion":1,"root":str(self.destination),"product":self.manifest["app"]["id"],"state":"applying",
                 "operations":[{"kind":"file","path":str(self.destination/"data.bin"),"existed":True,"backup":str(backup),"mode":420}]}
        (work/"journal.json").write_text(json.dumps(journal))
        self.install(setup,expected=1)
        execute([setup,"--recover","--dir",self.destination])
        self.assertFalse(work.exists());self.assertEqual((self.destination/"data.bin").read_bytes(),original)


@unittest.skipUnless(sys.platform.startswith("linux"), "XJ380 artifact generation is tested on the Linux build host")
class XjArtifactTests(unittest.TestCase):
    def test_elf_and_epf_native_payload(self):
        stub=BUILD/"ap-stubs/ap-setup-xj380-x64-tui.elf"
        if not stub.exists():self.skipTest("Build XJ380 stubs to enable artifact checks")
        with tempfile.TemporaryDirectory(prefix="accretion-xj-test-",dir=BUILD) as temporary:
            root=Path(temporary);payload=root/"payload";payload.mkdir()
            shutil.copyfile(stub,payload/"native.elf")
            manifest={"schemaVersion":1,"app":{"id":"test.accretion.xj380","name":"Native XAPI demo","version":"1.0"},
                      "inputs":{"xj380-x64":str(payload)},"applications":{"main":{"xj380":"native.elf"}}}
            project=root/"project.json";project.write_text(json.dumps(manifest))
            for ui in ("gui","tui"):
                binary=root/(ui+".epf")
                execute([APACK,"build","--project",project,"--target","xj380-x64","--ui",ui,"--format","epf","--output",binary])
                data=binary.read_bytes()
                self.assertEqual(data[:4],b"$EPF")
                self.assertEqual(struct.unpack_from("<HH",data,12),(0x0380,1))
                table=struct.unpack_from("<Q",data,56)[0];count=struct.unpack_from("<I",data,80)[0]
                segments=[struct.unpack_from("<IIQQQQQ",data,table+64*i) for i in range(count)]
                loads=[s for s in segments if s[0]==1]
                self.assertGreaterEqual(len(loads),3)
                self.assertFalse(any((s[1]&6)==6 for s in loads),"Native code and embedded data must not be writable and executable together")
                self.assertFalse(any(s[0] in (2,3) for s in segments),"Native runtime must not import a dynamic libc or interpreter")
                inspected=json.loads(execute([APACK,"inspect","--package",binary]).stdout)
                self.assertEqual(inspected["runtime"]["stub"],"_ap_assets/runtime.elf")
                self.assertIn("_ap_assets/runtime.elf",{f["path"] for f in inspected["files"]})
                payload_segment=next(s for s in loads if data[s[2]:s[2]+8]==b"ACPKG001")
                corrupted=bytearray(data);corrupted[payload_segment[2]+payload_segment[4]-1]^=0x51
                binary.write_bytes(corrupted)
                execute([APACK,"inspect","--package",binary],expected=1)
            elf=stub.read_bytes()
            self.assertEqual(elf[:4],b"\x7fELF")
            table=struct.unpack_from("<Q",elf,32)[0];stride,count=struct.unpack_from("<HH",elf,54)
            types=[struct.unpack_from("<I",elf,table+i*stride)[0] for i in range(count)]
            self.assertNotIn(2,types);self.assertNotIn(3,types)

@unittest.skipUnless(ARGS.windows or os.name=="nt","Windows native tests were not requested")
class WindowsTests(unittest.TestCase):
    def test_windows_native_gui_tui_and_uninstall(self):
        with tempfile.TemporaryDirectory(prefix="accretion-win-test-",dir=BUILD) as temporary:
            root=Path(temporary);payload=root/"应用 payload";payload.mkdir()
            shutil.copyfile(BUILD/"ap-stubs/ap-setup-windows-x64-tui.exe",payload/"toolbox.exe")
            (payload/"data.bin").write_bytes(b"Windows payload"*1000)
            (payload/"assets").mkdir();(payload/"assets/配置.txt").write_text("nested UTF-8 payload",encoding="utf-8")
            manifest={"schemaVersion":1,"app":{"id":"test.accretion.windows","name":"Windows 测试应用","version":"1.0.0"},"inputs":{"windows-x64":str(payload)},"applications":{"main":{"windows":"toolbox.exe","arguments":["--help"]}},"ui":{"modes":["gui","tui"],"default":"tui"},"environment":[{"id":"home","name":"AP_NATIVE_TEST_HOME","value":"${install.dir}","operation":"set","defaultChecked":True}],"shortcuts":[{"id":"desktop","name":"测试快捷方式","application":"main","location":"desktop","defaultChecked":True}]}
            dib=struct.pack("<IiiHHIIiiII",40,2,4,1,32,0,16,0,0,0,0)+b"\x00\x88\xff\xff"*4+b"\x00"*8
            icon=root/"安装图标.ico";icon.write_bytes(b"\x00\x00\x01\x00\x01\x00"+struct.pack("<BBBBHHII",2,2,0,0,1,32,len(dib),22)+dib)
            manifest["branding"]={"installerIcon":str(icon),"applicationIcon":str(icon)}
            project=root/"项目 配置.json";project.write_text(json.dumps(manifest,ensure_ascii=False),encoding="utf-8")
            setup=root/"安装 程序.exe"
            execute([APACK,"build","--project",project,"--target","windows-x64","--ui","tui","--output",setup])
            destination=root/"Windows 安装 空格";state=root/"isolated"
            execute([setup,"--silent","--dir",native_windows(destination),"--state-root",native_windows(state)])
            self.assertEqual((destination/"data.bin").read_bytes(),(payload/"data.bin").read_bytes())
            self.assertEqual((destination/"assets/配置.txt").read_bytes(),(payload/"assets/配置.txt").read_bytes())
            self.assertEqual((destination/"_ap_assets/applicationIcon.ico").read_bytes(),icon.read_bytes())
            self.assertTrue((state/"desktop/测试快捷方式.lnk").is_file())
            receipt=json.loads((destination/".accretion/receipt.json").read_text(encoding="utf-8"))
            self.assertEqual(receipt["platform"],"windows")
            gui=root/"gui.exe"
            execute([APACK,"build","--project",project,"--target","windows-x64","--ui","gui","--output",gui])
            execute([gui,"--gui-smoke-test"])
            execute([destination/".accretion/uninstall.exe","--silent"])
            deadline=time.monotonic()+15
            while destination.exists() and time.monotonic()<deadline:time.sleep(.05)
            self.assertFalse(destination.exists(),"Windows uninstall must remove its executing application directory")
            self.assertFalse((state/"desktop/测试快捷方式.lnk").exists())


if __name__=="__main__":
    if not APACK.exists():raise SystemExit("Build apack first: python3 tools/build.py")
    unittest.main(argv=[__file__,*UNIT_ARGS],verbosity=2)
