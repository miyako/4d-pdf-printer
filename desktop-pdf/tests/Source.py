"""Source/provenance checks only. Does not execute or emulate Windows APIs."""
from pathlib import Path
import re
import subprocess
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
DEPENDENCY = "8b34e5669bec822bb82768fd61b0483bbdd74567"
PROOF = "12cc27915d08fcf29c5b1f00c422e0a16cc23e1b"
NS = {
    "m": "http://schemas.microsoft.com/appx/manifest/foundation/windows10",
    "u10": "http://schemas.microsoft.com/appx/manifest/uap/windows10/10",
    "desktop": "http://schemas.microsoft.com/appx/manifest/desktop/windows10",
    "psa": "http://schemas.microsoft.com/appx/manifest/printsupport/windows10/2",
}


def original(ref, path):
    return subprocess.check_output(["git", "show", f"{ref}:{path}"], cwd=ROOT)


class Source(unittest.TestCase):
    def test_dependency_snapshot_exact(self):
        names = subprocess.check_output(
            ["git", "ls-tree", "-r", "--name-only", DEPENDENCY, "--", "renderer", "docs/renderer"],
            cwd=ROOT, text=True,
        ).splitlines()
        actual = subprocess.check_output(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--",
             "renderer", "docs/renderer"], cwd=ROOT, text=True,
        ).splitlines()
        self.assertEqual(len(names), 33)
        self.assertEqual(set(actual), set(names), "Unexpected/missing dependency source")
        for name in names:
            with self.subTest(path=name):
                self.assertEqual((ROOT / name).read_bytes(), original(DEPENDENCY, name))

    def test_corrected_geometry_is_exact_extraction(self):
        self.assertEqual(
            (ROOT / "desktop-pdf/tests/PhysicalMapping.h").read_bytes(),
            original(PROOF, "desktop/proof/PhysicalMapping.h"),
        )

    def test_distinct_identity_and_endpoints(self):
        package = ET.parse(ROOT / "desktop-pdf/package/AppxManifest.xml").getroot()
        identity = package.find("m:Identity", NS)
        self.assertEqual(identity.attrib["Name"], "Miyako.DesktopPdfIntegration")
        self.assertEqual(identity.attrib["Publisher"], "CN=DesktopPdfPrototypeDev")
        self.assertEqual(identity.attrib["Version"], "0.1.0.0")
        apps = {app.attrib["Id"]: app for app in package.findall("m:Applications/m:Application", NS)}
        self.assertEqual(set(apps), {"SaveAs", "Silent", "Client"})
        for app_id, uri in (("SaveAs", "desktop-pdf:save-as"), ("Silent", "desktop-pdf:silent")):
            app = apps[app_id]
            queue = app.find("m:Extensions/psa:Extension/psa:PrintSupportVirtualPrinter", NS)
            self.assertEqual(queue.attrib["PrinterUri"], uri)
            self.assertEqual(queue.attrib["PreferredInputFormat"], "application/oxps")
            self.assertEqual(queue.attrib.get("OutputFileTypes"), "pdf" if app_id == "SaveAs" else None)
            extension = app.find("m:Extensions/desktop:Extension", NS)
            self.assertEqual(extension.attrib, {
                "Category": "windows.fullTrustProcess", "Executable": "IntegrationHost.exe",
            })
        client = apps["Client"]
        self.assertEqual(client.attrib[f"{{{NS['u10']}}}RuntimeBehavior"], "packagedClassicApp")
        self.assertEqual(client.attrib[f"{{{NS['u10']}}}TrustLevel"], "mediumIL")

    def test_labels_and_pdc_stage_wiring(self):
        resw = ET.parse(ROOT / "desktop-pdf/package/Strings/en-US/Resources.resw").getroot()
        self.assertEqual({item.attrib["name"]: item.findtext("value") for item in resw}, {
            "SaveAsQueue": "4D PDF Integration (Save As)",
            "SilentQueue": "4D PDF Integration (Silent)",
        })
        cmake = (ROOT / "desktop-pdf/CMakeLists.txt").read_text()
        self.assertIn("DiagnosticPdc.xml", cmake)
        self.assertIn("IntegrationPdc.xml", cmake)
        self.assertIn('CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64"', cmake)
        self.assertIn("set(PDF_PACKAGE_ARCH arm64)", cmake)
        self.assertIn('install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml"', cmake)

    def test_workflow_execution_contract(self):
        # Deliberately static guards, not a YAML parser or Windows execution proof.
        text = (ROOT / ".github/workflows/renderer-windows.yml").read_text()
        header, jobs = text.split("\njobs:\n", 1)
        own, renderer = jobs.split("\n  standalone:\n", 1)
        self.assertTrue(own.startswith("  integration:\n"))
        self.assertIn("  pull_request:\n", header)
        self.assertIn("  push:\n    branches: [main]\n", header)
        for path in ("renderer/**", "docs/renderer/**", "desktop-pdf/**",
                     "printer/psa/App.cpp", "printer/package/DiagnosticPdc.xml",
                     "printer/BuildSupport.ps1", ".github/workflows/renderer-windows.yml"):
            self.assertEqual(header.count(f"'{path}'"), 2, path)
        self.assertIn("options: [own-code, full]\n        default: full\n        required: true", header)
        self.assertRegex(header, r"expected_sha:\n(?:        .*\n)*?        required: true")
        self.assertIn("permissions:\n  contents: read\n", header)
        self.assertIn("GIT_CONFIG_COUNT: '1'", header)
        self.assertIn("GIT_CONFIG_KEY_0: core.autocrlf", header)
        self.assertIn("GIT_CONFIG_VALUE_0: 'false'", header)
        self.assertEqual(text.count("ref: ${{ github.event.pull_request.head.sha || github.sha }}"), 2)
        self.assertEqual(text.count("persist-credentials: false"), 2)
        self.assertNotIn("refs/pull/", text)
        for section in (own, renderer):
            self.assertIn("fail-fast: false", section)
            for arch, runner, vs in (("x64", "windows-2022", "2022"),
                                     ("arm64", "windows-11-arm", "2026")):
                self.assertIn(f"- arch: {arch}\n            runner: {runner}\n"
                              f"            visualStudio: '{vs}'", section)
        self.assertIn("EXPECTED_SHA: ${{ inputs.expected_sha }}", own)
        self.assertIn("SELECTED_SCOPE: ${{ github.event_name == 'workflow_dispatch' && inputs.scope || 'full' }}", own)
        self.assertIn("$head -cne $env:SOURCE_SHA", own)
        self.assertIn("'^[0-9a-fA-F]{40}$'", own)
        self.assertIn("$head -cne $env:EXPECTED_SHA.ToLowerInvariant()", own)
        self.assertIn("Automatic events must run full regressions.", own)
        self.assertLess(own.index("id: guard"), own.index("git fetch"))
        self.assertLess(own.index("id: guard"), own.index("Checked cmake"))
        self.assertIn(f"git fetch --no-tags --depth=1 origin {DEPENDENCY} {PROOF}", own)
        self.assertIn("$actual -cne $env:TARGET_ARCH", own)
        self.assertIn("Get-PrinterBuildPlan", own)
        self.assertIn("[version]'4.2.0'", own)
        self.assertIn("[version]'3.24.0'", own)
        self.assertIn("-T', 'v143'", own)
        self.assertIn("-DCMAKE_SYSTEM_VERSION=10.0.26100.0", own)
        self.assertIn("Windows Kits/10/Include/10.0.26100.0/um/Windows.h", own)
        self.assertIn("Assert-PrinterPeMachine", own)
        for binary in ("IntegrationApp.exe", "IntegrationTask.dll", "IntegrationHost.exe",
                       "IntegrationClient.exe", "IntegrationContract.exe", "IntegrationNative.exe"):
            self.assertIn(f"'{binary}'", own)
        ctest = re.search(r"Checked ctest @\((.*?)\)", own, re.S).group(1)
        self.assertIn("'--no-tests=error'", ctest)
        self.assertNotRegex(ctest, r"'-(?:R|E|L|LE)'")
        self.assertIn("desktop-pdf/tests/Source.py", own)
        self.assertIn("git show --format= --check HEAD", own)
        for forbidden in ("renderer/tools/build-windows.ps1", "actions/setup-python@",
                          "pip install", "vcpkg.exe", "makeappx", "makepri", "--install"):
            self.assertNotIn(forbidden, own)
        self.assertIn("needs: integration\n", renderer)
        self.assertIn("if: ${{ needs.integration.result == 'success' && "
                      "(github.event_name != 'workflow_dispatch' || inputs.scope == 'full') }}", renderer)
        stages = [renderer.index(f"-Stage {stage}") for stage in ("Prepare", "Build", "Test")]
        self.assertEqual(stages, sorted(stages))
        self.assertEqual(renderer.count("& renderer/tools/build-windows.ps1"), 3)
        self.assertNotIn("inventory-windows.ps1", renderer)
        self.assertNotIn("-RuntimeRoot", renderer)
        self.assertIn("VCPKG_BINARY_SOURCES: clear", renderer)
        self.assertNotIn("continue-on-error", text)
        self.assertNotIn("actions/cache@", text)
        self.assertNotIn("[skip ci]", text)
        uploads = re.findall(r"(?m)^          path: \|\n((?:            .+\n)+)", text)
        self.assertEqual(len(uploads), 2)
        self.assertEqual({line.strip() for line in uploads[0].splitlines()}, {
            "${{ env.EVIDENCE }}/receipt.json", "${{ env.EVIDENCE }}/pe-machines.json",
            "${{ env.EVIDENCE }}/source-checks.txt", "${{ env.EVIDENCE }}/build-and-tests.txt",
            "${{ env.EVIDENCE }}/native-ctest.txt",
        })
        self.assertEqual({line.strip() for line in uploads[1].splitlines()}, {
            "renderer/build-evidence-${{ matrix.arch }}/runtime-imports.json",
            "renderer/build-evidence-${{ matrix.arch }}/build-provenance.json",
            "renderer/build-evidence-${{ matrix.arch }}/native-test-receipt.json",
            "renderer/build-evidence-${{ matrix.arch }}/test-results.txt",
            "renderer/build-evidence-${{ matrix.arch }}/notices/*/copyright",
            "renderer/build-evidence-${{ matrix.arch }}/notices/*/vcpkg.spdx.json",
            "renderer/build-windows-${{ matrix.arch }}/Testing/Temporary/LastTest.log",
        })
        for receipt_field in ("renderer_executed", "psa_activation_tested", "publication_tested",
                              "packaging_performed", "productUsable"):
            self.assertIn(f"{receipt_field} = $false", own)
        self.assertIn("NATIVE_OUTCOME: ${{ steps.native.outcome }}", own)
        self.assertIn("head_verified = ($env:GUARD_OUTCOME -eq 'success')", own)
        for field in ("requested_visual_studio", "requested_sdk", "requested_toolset"):
            self.assertIn(f"{field} =", own)


if __name__ == "__main__":
    unittest.main()
