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
    def test_activation_diagnostics_are_separate_and_bounded(self):
        trace = (ROOT / "desktop-pdf/ActivationDiagnostics.h").read_text()
        task = (ROOT / "desktop-pdf/Task.cpp").read_text()
        self.assertIn("EventLimit = 32, IdentityChars = 256, RecordLimit = 8192", trace)
        self.assertIn("accepted == EventLimit", trace)
        self.assertIn('L"4d-pdf-printer.activation-diagnostic"', trace)
        self.assertIn('L"ActivationDiagnostics"', trace)
        self.assertIn("GetCurrentApplicationUserModelId(&length, buffer.data())", trace)
        self.assertIn("identity.error == APPMODEL_ERROR_NO_APPLICATION", trace)
        for state in ('L"available"', 'L"unavailable"', 'L"invalid-response"', 'L"error"'):
            self.assertIn(state, trace)
        self.assertIn('L"identity_error"', trace)
        self.assertIn('L"identity_validation_error"', trace)
        self.assertIn("CoIncrementMTAUsage(&cookie)", trace)
        self.assertIn("CoDecrementMTAUsage(cookie)", trace)
        self.assertIn("Require(type == APTTYPE_MTA", trace)
        writer = trace.split("static fire_and_forget Drain(", 1)[1].split("\npublic:\n", 1)[0]
        self.assertLess(writer.index("co_await resume_background()"), writer.index("Apartment apartment"))
        self.assertLess(writer.index("co_await resume_background()"), writer.index("co_await Root()"))
        self.assertIn("if (failed) return;", trace)
        self.assertIn("self->failed = true; self->writing = false;", writer)
        self.assertIn("if (self->failed || !self->journal.Pop(event))", writer)
        self.assertLess(writer.index("self->failed ||"), writer.index("self->journal.Pop(event)"))
        enqueue_failure = trace.split('Failure(L"activation diagnostics enqueue"', 1)[1]
        self.assertIn("failed = true;", enqueue_failure)
        self.assertNotIn("writing = false", enqueue_failure)
        self.assertIn('Failure(L"activation diagnostics write", hr)', trace)
        self.assertNotIn("get_strong", trace)
        self.assertNotIn("BackgroundTaskDeferral", trace)
        for forbidden in ("JobTitle", "destination", "SourceContent", "source.oxps",
                          "Jobs", "Registrations", ".get()", "init_apartment", "std::thread",
                          "StartDoc", "SetPrinter", "CreateEvent", "WaitForSingleObject"):
            self.assertNotIn(forbidden, trace)
        add = trace.split("void Add(", 1)[1]
        self.assertNotIn("co_await", add)
        self.assertNotIn("Publish(", add)
        begin = task.split("fire_and_forget Begin()", 1)[1].split("fire_and_forget Data(", 1)[0]
        before_start = begin.split("session.Start();", 1)[0]
        self.assertNotIn("co_await", before_start)
        self.assertIn("Trace(activation::Point::StartBefore)", before_start)
        self.assertIn("Trace(activation::Point::StartReturned)", begin)
        self.assertLess(begin.index("auto deadline = GetTickCount64() + 45000;"),
                        begin.index("Trace(activation::Point::StartReturned)"))
        metadata = task.split('stage = L"capture"', 1)[0]
        for point in ("RunEnter", "TriggerBefore", "TriggerAcquired", "SessionBefore", "SessionSubscribed",
                      "DataEnter", "EndpointChecked", "ContentChecked", "ConfigurationChecked",
                      "SelectorChecked", "RegistrationChecked", "SubmissionChecked", "MetadataValidated"):
            self.assertIn(f"activation::Point::{point}", metadata)
        self.assertIn("Trace(job ? activation::Point::DataFailure : activation::Point::PreJobFailure, primary)", task)
        self.assertLess(task.index("auto timeout = DefaultCompletionMs, deadline = Deadline(Now(), timeout);"),
                        task.index("Trace(activation::Point::ConfigurationChecked)"))
        factory = task.split("struct Factory", 1)[1]
        self.assertNotIn("activation::", factory)

    def test_activation_instrumentation_preserves_job_finalizer(self):
        task = (ROOT / "desktop-pdf/Task.cpp").read_text()
        baseline = original("ae53b92a5e1f3fa442f41246079cde7e2f4afd47", "desktop-pdf/Task.cpp").decode()
        # No trace additions or behavior edits in capture/launch/wait/publication.
        start = '            if (mode == Mode::Silent) {\n                auto claim'
        end = '        } catch (...) {\n            primary = to_hresult();'
        self.assertEqual(task.split(start, 1)[1].split(end, 1)[0],
                         baseline.split(start, 1)[1].split(end, 1)[0])
        self.assertEqual(task.count("args.CompleteJob(status);"), 1)
        self.assertIn("auto deadline = GetTickCount64() + 45000;", task)
        self.assertIn("if (!working.exchange(true) && !finished)", task)
        self.assertIn("if (finished.exchange(true)) return;", task)
        self.assertIn("if (!self->working.exchange(true)) self->Finish();", task)
        self.assertIn("primary = to_hresult(); Trace(activation::Point::CompleteFailure, primary);", task)
        self.assertIn("Failure(stage.c_str(), primary);", task)
        # The SDK COM header declares this export without noexcept; body/factory stay exact.
        unload = 'extern "C" HRESULT __stdcall DllCanUnloadNow()'
        baseline_factory = baseline.split("struct Factory", 1)[1]
        self.assertEqual(baseline_factory.count(unload + " noexcept"), 1)
        self.assertEqual(task.split("struct Factory", 1)[1],
                         baseline_factory.replace(unload + " noexcept", unload, 1))

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
        self.assertEqual(identity.attrib["Version"], "0.1.0.1")
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
        native_targets = re.search(r"foreach\(target (.*?)\)(.*?)endforeach\(\)", cmake, re.S)
        self.assertEqual(set(native_targets.group(1).split()), {
            "IntegrationContract", "IntegrationWorkerProcess", "IntegrationTask", "IntegrationApp",
            "IntegrationHost", "IntegrationClient", "IntegrationNative", "IntegrationProcessTests",
            "IntegrationWorkerFixture",
        })
        self.assertIn("UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX", native_targets.group(2))
        self.assertIn("/W4 /WX /EHsc /permissive- /utf-8", native_targets.group(2))
        self.assertIn("target_link_libraries(${target} PRIVATE windowsapp ole32)", native_targets.group(2))
        self.assertIn('CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64"', cmake)
        self.assertIn("set(PDF_PACKAGE_ARCH arm64)", cmake)
        self.assertIn('install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml"', cmake)
        installed = re.search(r"install\(TARGETS (.*?) RUNTIME", cmake).group(1).split()
        self.assertEqual(set(installed), {"IntegrationApp", "IntegrationTask", "IntegrationHost", "IntegrationClient"})
        self.assertIn("IntegrationProcessTests PRIVATE IntegrationWorkerProcess", cmake)
        cases = re.search(r"foreach\(case (.*?)\)", cmake).group(1).split()
        self.assertEqual(set(cases), {
            "success", "crash", "deadline", "cancel", "precancel", "malformed", "duplicate",
            "trailing", "missing", "mismatch", "exit24", "exit71", "descendant", "breakaway",
            "handles", "channel", "killclose", "publish", "existing", "race", "pubcancel",
            "journal", "validation",
        })
        self.assertEqual(len(cases), 23)
        process = (ROOT / "desktop-pdf/tests/Process.cpp").read_text()
        for call in ("Render(", "PublishNew(", "Reconcile(", "Observe(", "area->StopOwned()"):
            self.assertIn(call, process)
        self.assertIn('std::string_view(error.what()) == "destination exists/unsupported"', process)
        self.assertIn('Require(existingReason, "expected exact existing-file semantic rejection', process)
        self.assertLess(process.index("Original publication rejection HRESULT="), process.index('"publication branch outcome"'))
        self.assertIn("std::rethrow_exception(primary)", process)
        worker = (ROOT / "desktop-pdf/WorkerProcess.cpp").read_text()
        self.assertIn('IsWellKnownSid(sid, WinBuiltinAdministratorsSid) ||', worker)
        self.assertIn('NonPropagatingDirectoryTraversalGrant(ace->Mask, header->AceFlags)', worker)
        header = (ROOT / "desktop-pdf/WorkerProcess.h").read_text()
        self.assertIn("inline constexpr bool NonPropagatingDirectoryTraversalGrant(ACCESS_MASK mask, BYTE flags) noexcept", header)
        self.assertIn("constexpr ACCESS_MASK allowed = FILE_TRAVERSE | SYNCHRONIZE;", header)
        self.assertIn("(flags == 0 || flags == INHERITED_ACE) && (mask & ~allowed) == 0", header)
        self.assertIn("NonPropagatingDirectoryTraversalGrant(0x00100020u, 0)", process)
        self.assertIn("constexpr ACCESS_MASK subsets[]{0, FILE_TRAVERSE, SYNCHRONIZE, allowed};", process)
        self.assertIn("flags != 256", process)
        self.assertIn("bit != 32", process)
        self.assertIn("NonPropagatingDirectoryTraversalGrant(right | allowed, flags)", process)
        validation = process.split("void Validation(", 1)[1]
        self.assertLess(validation.index("bit != 32"), validation.index("SafeParents("))
        self.assertNotIn("S-1-", worker + header)
        for forbidden in ("SetNamedSecurityInfo", "SetSecurityInfo", "SetFileSecurity"):
            self.assertNotIn(forbidden, worker + header + process)
        for field in ("; directory=", "; sid=", "; ace_type=", "; ace_flags=", "; access_mask="):
            self.assertIn(field, worker)
        self.assertIn("ConvertSidToStringSidW", worker)
        self.assertNotIn("LookupAccountSid", worker)
        host = (ROOT / "desktop-pdf/Host.cpp").read_text()
        self.assertLess(host.index("ConfiguredRuntime(root)"), host.index("Render(runtime,"))
        helper = (ROOT / "desktop-pdf/build.ps1").read_text()
        self.assertIn("$identity.Version -cne '0.1.0.1'", helper)
        self.assertIn("'Build', 'Package', 'SignCopy'", helper)
        self.assertIn("No automatic fetch/install.", helper)
        for ref in (DEPENDENCY, PROOF):
            self.assertIn(ref, helper)
        for forbidden in ("New-SelfSignedCertificate", "Import-Certificate", "Add-AppxPackage",
                          "Assert-PrinterPackage", "Assert-PrinterResourceDump", "printer/build.ps1",
                          "'/nv'", "Invoke-WebRequest", "git fetch"):
            self.assertNotIn(forbidden, helper)
        package = helper.split("} else {\n            $build = Get-Content", 1)[1]
        self.assertNotIn("'--build'", package)
        self.assertNotIn("Checked $ctest", package)
        self.assertIn("PackageEvidence $package $pri", package)

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
        self.assertIn(f"git fetch --no-tags --depth=1 origin {DEPENDENCY} {PROOF} "
                      "ae53b92a5e1f3fa442f41246079cde7e2f4afd47", own)
        self.assertIn("$actual -cne $env:TARGET_ARCH", own)
        self.assertIn("Get-PrinterBuildPlan", own)
        self.assertIn("[version]'4.2.0'", own)
        self.assertIn("[version]'3.24.0'", own)
        self.assertIn("-T', 'v143'", own)
        self.assertIn("-DCMAKE_SYSTEM_VERSION=10.0.26100.0", own)
        self.assertIn("Windows Kits/10/Include/10.0.26100.0/um/Windows.h", own)
        self.assertIn("Assert-PrinterPeMachine", own)
        for binary in ("IntegrationApp.exe", "IntegrationTask.dll", "IntegrationHost.exe",
                       "IntegrationClient.exe", "IntegrationContract.exe", "IntegrationNative.exe",
                       "IntegrationProcessTests.exe", "IntegrationWorkerFixture.exe"):
            self.assertIn(f"'{binary}'", own)
        ctest = re.search(r"Checked ctest @\((.*?)\)", own, re.S).group(1)
        self.assertIn("'--no-tests=error'", ctest)
        self.assertNotRegex(ctest, r"'-(?:R|E|L|LE)'")
        self.assertIn("desktop-pdf/tests/Source.py", own)
        self.assertIn("git show --format= --check HEAD", own)
        for forbidden in ("renderer/tools/build-windows.ps1", "actions/setup-python@",
                          "pip install", "vcpkg.exe", "makeappx", "makepri", "--install", "-Stage SignCopy"):
            self.assertNotIn(forbidden, own)
        self.assertIn("if: ${{ success() && steps.native.outcome == 'success' }}", own)
        self.assertIn("./desktop-pdf/build.ps1 -Stage Package", own)
        self.assertLess(own.index("Checked ctest"), own.index("id: package"))
        self.assertIn("PACKAGE_OUTCOME: ${{ steps.package.outcome }}", own)
        self.assertIn("packaging_outcome = $env:PACKAGE_OUTCOME", own)
        self.assertIn("unsigned_package_verified = ($env:PACKAGE_OUTCOME -eq 'success')", own)
        self.assertIn("schema = '4d-pdf-printer.own-code-ci'; version = 2", own)
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
            "${{ env.EVIDENCE }}/package-evidence.json",
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
        for receipt_field in ("renderer_executed", "psa_activation_tested", "packaged_publication_tested", "productUsable"):
            self.assertIn(f"{receipt_field} = $false", own)
        self.assertIn("NATIVE_OUTCOME: ${{ steps.native.outcome }}", own)
        self.assertIn("head_verified = ($env:GUARD_OUTCOME -eq 'success')", own)
        for field in ("requested_visual_studio", "requested_sdk", "requested_toolset"):
            self.assertIn(f"{field} =", own)


if __name__ == "__main__":
    unittest.main()
