"""Source/provenance checks only. Does not execute or emulate Windows APIs."""
from pathlib import Path
import hashlib
import re
import subprocess
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
DEPENDENCY = "8b34e5669bec822bb82768fd61b0483bbdd74567"
PROOF = "12cc27915d08fcf29c5b1f00c422e0a16cc23e1b"
INTEGRATION_BASELINE = "245373012067c928781e5c85b7aafddd191b800d"
JOURNAL_BASELINE = "66740903d715cbd9a6572486eefb83c4fe7b9375"
OBSERVATION_BASELINE = "5f2382888fe8b892a83b07c832654818f7ec2fdd"
NS = {
    "m": "http://schemas.microsoft.com/appx/manifest/foundation/windows10",
    "u10": "http://schemas.microsoft.com/appx/manifest/uap/windows10/10",
    "desktop": "http://schemas.microsoft.com/appx/manifest/desktop/windows10",
    "psa": "http://schemas.microsoft.com/appx/manifest/printsupport/windows10/2",
}


def original(ref, path):
    return subprocess.check_output(["git", "show", f"{ref}:{path}"], cwd=ROOT)


class Source(unittest.TestCase):
    def test_scoped_build_profile_is_not_full_suite(self):
        helper = (ROOT / "desktop-pdf/build.ps1").read_text()
        checks = (ROOT / "desktop-pdf/BuildValidation.ps1").read_text()
        self.assertIn("$ValidationProfile = 'full-suite'", helper)
        self.assertIn("Clean exact-source checkout required", helper)
        self.assertIn("Assert-BuildValidation $build $ValidationProfile $sha $sourceTree", helper)
        self.assertIn("Unsigned package validation profile mismatch.", helper)
        self.assertIn("'IntegrationApp', 'IntegrationTask', 'IntegrationHost', 'IntegrationClient', 'IntegrationContract', 'IntegrationNative'", helper)
        self.assertIn("Checked $ctest @('--test-dir'", helper)
        scoped = helper.split("$checks = @(Invoke-ScopedBuildChecks", 1)[1].split("$receipt.build_directory", 1)[0]
        self.assertNotIn("ctests = 'passed'", scoped)
        self.assertIn("validation_status='passed'", scoped)
        self.assertIn("Scoped source changed during checks", scoped)
        self.assertIn("@('contract','isolated-journal-v3')", checks)
        self.assertIn("@('--isolated-journal',$testRoot)", checks)
        self.assertNotIn("ctest", checks.split("function Invoke-ScopedBuildChecks", 1)[1])
        self.assertNotIn("Remove-Item", checks)
        self.assertNotIn("Stop-Process", checks)
        self.assertIn("started_utc=", checks)
        self.assertIn("image_after=", checks)
        self.assertIn("Exact four scoped payloads required", checks)
        self.assertIn("Scoped compiler/SDK/tool facts mismatch", checks)

    def test_isolated_native_task_seam_only(self):
        task = (ROOT / "desktop-pdf/Task.cpp").read_text()
        seam = """private:
    std::shared_ptr<activation::Trace> const diagnosticOverride;
public:
    Task() = default;
    explicit Task(std::shared_ptr<activation::Trace> trace) : diagnosticOverride(std::move(trace)) {
        Require(diagnosticOverride != nullptr, "explicit task diagnostic override");
    }
"""
        self.assertEqual(task.count(seam), 1)
        old = task.replace(seam, "", 1).replace(
            "diagnosticOverride ? diagnosticOverride : std::make_shared<activation::Trace>()",
            "std::make_shared<activation::Trace>()", 1)
        self.assertEqual(hashlib.sha256(old.encode()).hexdigest(),
                         "82affff73b14e79f1a9d9e3c9f40be0ba25919b1de07ded72226c062af7a2d19")
        run = task.split("void Run(", 1)[1]
        self.assertLess(run.index("diagnostics = diagnosticOverride ?"), run.index("Point::RunEnter"))
        self.assertLess(run.index("Point::RunEnter"), run.index("task.GetDeferral()"))
        self.assertLess(run.index("task.GetDeferral()"), run.index("task.Canceled("))
        for path, digest in {
            "ActivationDiagnostics.h": "991cbd412b9e5c41bf4510c1133f9642edf5f1cdc93db48817591294c843f8c4",
            "ActivationObservations.h": "16c76b4c965387ba4ce9c73b2485dc2d1d35f6bcac68eab2a88589e143fb65b2",
            "ObservationWire.h": "23b4daaa921de9ffe581c3d8f9cd2cf795c464aa457e93f9ea325b01172a67f5",
            "tools/journal.py": "0348fc6c6acfe644d1d7854193f22bb2a1fe8daed9f3811f3bdcf17b9e4deb68",
        }.items():
            self.assertEqual(hashlib.sha256((ROOT / "desktop-pdf" / path).read_bytes()).hexdigest(), digest)

    def test_isolated_native_selector_and_retention(self):
        native = (ROOT / "desktop-pdf/tests/Native.cpp").read_text()
        selected = native.split("if (argc != 1) {", 1)[1].split("\n        init_apartment", 1)[0]
        self.assertIn('argc == 3 && std::wstring_view(argv[1]) == L"--isolated-journal"', selected)
        self.assertIn("tests::FreshTrialRoot root(argv[2]);", selected)
        self.assertIn("tests::NativeJournalChecks(&root);", selected)
        self.assertIn("return 0;", selected)
        for forbidden in ("make_shared", "JournalChecks();", "ReportChecks();", "LifetimeChecks("):
            self.assertNotIn(forbidden, selected)
        journal = (ROOT / "desktop-pdf/tests/ActivationJournalNative.h").read_text()
        self.assertNotIn("TrialDirectory directory;", journal)
        directory = journal.split("struct TrialDirectory", 1)[1].split("struct TrialIo", 1)[0]
        self.assertIn("retained(suite != nullptr)", directory)
        self.assertLess(directory.index("if (retained) return;"), directory.index("remove_all"))
        self.assertIn("if (suite) path = suite->NewCase();", directory)
        for label in ("successful case tree retained", "failed case tree retained", "pending owner tree retained"):
            self.assertIn(label, journal)
        root = (ROOT / "desktop-pdf/tests/NativeTrialRoot.h").read_text()
        for forbidden in ("remove_all", "remove(", "temp_directory", "Package::", "ApplicationData::"):
            self.assertNotIn(forbidden, root)

    def test_isolated_native_injection_and_lifetime(self):
        journal = (ROOT / "desktop-pdf/tests/ActivationJournalNative.h").read_text()
        self.assertNotIn("make_shared<Trace>()", journal)
        self.assertNotIn("Package::Current", journal)
        self.assertNotIn("ApplicationData::Current", journal)
        self.assertIn("make_self<::Task>(std::shared_ptr<Trace>{})", journal)
        reentry = journal.split("for (unsigned instanceIndex", 1)[1].split("for (bool dataOwner", 1)[0]
        self.assertIn("instanceIndex < 2", reentry)
        self.assertIn("make_self<::Task>(injected)", reentry)
        self.assertIn("task->Run(instance.as<IBackgroundTaskInstance>());", reentry)
        self.assertIn("task->diagnostics == injected", reentry)
        self.assertLess(reentry.index("task->diagnostics.reset()"), reentry.index("!weak.expired()"))
        self.assertLess(reentry.index("task = nullptr;"), reentry.index("ReleasedTest(weak)"))
        self.assertEqual(journal.count("std::wstring Cache() override { return path; }"), 2)
        for forbidden in ("StorageFolder", "StorageFile", "FileIO::", "IAsyncInfo"):
            self.assertNotIn(forbidden, journal)
        report = journal.split("activation::ReportResult Report(", 1)[1].split("struct TestDeferral", 1)[0]
        self.assertIn("activation::NativeReportBackend backend(path, id);", report)
        self.assertNotIn("JournalIo::Report", report)

    def test_isolated_native_root_and_io_guards(self):
        root = (ROOT / "desktop-pdf/tests/NativeTrialRoot.h").read_text()
        constructor = root.split("explicit FreshTrialRoot(", 1)[1]
        self.assertLess(constructor.index("TrialPathText("), constructor.index("GetDriveTypeW("))
        self.assertLess(constructor.index("Hold(current)"), constructor.index("create_directory(path)"))
        self.assertIn("DRIVE_FIXED", constructor)
        self.assertIn("FILE_FLAG_OPEN_REPARSE_POINT", root)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", root)
        self.assertNotIn("FILE_SHARE_DELETE", root)
        self.assertIn("OPEN_EXISTING", root)
        self.assertIn("path.parent_path().relative_path()", root)
        self.assertIn('path / (L"case-" + RandomId())', root)
        self.assertIn('part == L"." || part == L".."', root)
        self.assertIn('stem == L"CON"', root)
        self.assertIn("TrialPathChecks()", root)
        journal = (ROOT / "desktop-pdf/tests/ActivationJournalNative.h").read_text()
        self.assertEqual(journal.count("= Confined(target);"), 2)
        self.assertEqual(journal.count("= Confined(from);"), 1)
        self.assertEqual(journal.count("= Confined(to);"), 1)
        self.assertIn("Id(std::wstring_view(leaf).substr(8))", journal)
        self.assertNotIn("std::wstring_view(Confined(", journal)
        self.assertIn('leaf.ends_with(L".json")', journal)
        self.assertIn("FreshTrialRoot existing(suite->path.wstring());", journal)
        self.assertIn('io->Confined(suite->path.wstring() + L"\\\\DesktopPdf")', journal)
        self.assertIn("parent.find(L\"\\\\..\") == std::wstring::npos", journal)
        # 007: holding blocks the single native writer thread on a test-controlled event.
        write = journal.split("void Write(handle file, std::string_view bytes) override {", 1)[1].split("void Rename(", 1)[0]
        self.assertLess(write.index("SetEvent(entered.get());"), write.index("WaitTest(release.get(), 30000);"))
        self.assertLess(write.index("WaitTest(release.get(), 30000);"), write.index("JournalIo::Write(std::move(file), bytes);"))
        self.assertIn("SetEvent(writeExited.get())", write)
        self.assertIn("Native journal timing: %u records published in %.3f ms (%.3f ms/record)", journal)
        self.assertIn("fwprintf(stderr,", journal)

    def test_journal_preserves_routing_job_and_worker_sources(self):
        for path in ("printer/psa/App.cpp", "desktop-pdf/Client.cpp", "desktop-pdf/Contract.h",
                     "desktop-pdf/WorkerProcess.cpp", "desktop-pdf/WorkerProcess.h", "desktop-pdf/Job.h",
                     "printer/package/DiagnosticPdc.xml", "desktop-pdf/package/Strings/en-US/Resources.resw"):
            with self.subTest(path=path):
                self.assertEqual((ROOT / path).read_bytes(), original(INTEGRATION_BASELINE, path))
        helper = (ROOT / "desktop-pdf/HostArguments.h").read_text()
        self.assertIn('L"/InvokerPRAID: SaveAs --mode Silent --job "', helper)
        self.assertIn("return ParseActivation(raw.substr(owner.size()));", helper)
        self.assertIn("return ParseActivation(raw);", helper)
        host = (ROOT / "desktop-pdf/Host.cpp").read_text()
        baseline_host = original(INTEGRATION_BASELINE, "desktop-pdf/Host.cpp").decode()
        self.assertEqual(host, baseline_host.replace('#include "WorkerProcess.h"',
                         '#include "WorkerProcess.h"\n#include "HostArguments.h"', 1)
                         .replace("auto activation = ParseActivation(raw);", "auto activation = ParseHostActivation(raw);", 1))
        native = (ROOT / "desktop-pdf/tests/Native.cpp").read_text()
        self.assertIn('ParseHostActivation(L"/InvokerPRAID: SaveAs --mode Silent --job " + job)', native)
        self.assertIn('Validate(request, hostActivation.id, hostActivation.mode, L"request")', native)
        build = (ROOT / "desktop-pdf/build.ps1").read_text()
        baseline_build = original(INTEGRATION_BASELINE, "desktop-pdf/build.ps1").decode()
        prefix = build.split("function Manifest(", 1)[0]
        prefix = prefix.replace("    [ValidateSet('full-suite', 'isolated-journal-v3')][string]$ValidationProfile = 'full-suite',\n", "", 1)
        prefix = prefix.replace(". (Join-Path $root 'BuildValidation.ps1')\n", "", 1)
        self.assertEqual(prefix, baseline_build.split("function Manifest(", 1)[0])
        self.assertEqual(build.split("function PackageEvidence(", 1)[1].split("\nFresh $OutputRoot", 1)[0],
                         baseline_build.split("function PackageEvidence(", 1)[1].split("\nFresh $OutputRoot", 1)[0])
        self.assertIn("$receipt.app_ids = @('SaveAs', 'Client')", build)
        workflow = (ROOT / ".github/workflows/renderer-windows.yml").read_text()
        baseline_workflow = original(INTEGRATION_BASELINE, ".github/workflows/renderer-windows.yml").decode()
        fetch = f"git fetch --no-tags --depth=1 origin {DEPENDENCY} {PROOF} ae53b92a5e1f3fa442f41246079cde7e2f4afd47"
        self.assertEqual(baseline_workflow.count(fetch), 1)
        # The renderer Prepare step uses the inline VS expression that the pinned
        # renderer/tests/Check-BuildSupport.ps1 requires; the matrix maps to the same values.
        vs_matrix = "            -VisualStudioVersion '${{ matrix.visualStudio }}'"
        vs_inline = "            -VisualStudioVersion '${{ matrix.arch == 'arm64' && '2026' || '2022' }}'"
        self.assertEqual(baseline_workflow.count(vs_matrix), 1)
        self.assertEqual(workflow.count(vs_inline), 1)
        self.assertEqual(workflow, baseline_workflow.replace(
            fetch, fetch + " " + INTEGRATION_BASELINE + " " + JOURNAL_BASELINE + " " + OBSERVATION_BASELINE, 1
        ).replace(vs_matrix, vs_inline, 1))
        refs = re.search(r"git fetch --no-tags --depth=1 origin ([^\n]+)", workflow).group(1).split()
        self.assertEqual(refs, [DEPENDENCY, PROOF, "ae53b92a5e1f3fa442f41246079cde7e2f4afd47",
                                INTEGRATION_BASELINE, JOURNAL_BASELINE, OBSERVATION_BASELINE])

    def test_activation_diagnostics_are_separate_and_bounded(self):
        trace = (ROOT / "desktop-pdf/ActivationDiagnostics.h").read_text()
        task = (ROOT / "desktop-pdf/Task.cpp").read_text()
        self.assertIn("EventLimit = 32, IdentityChars = 256, RecordLimit = 8192", trace)
        self.assertIn("++counter;", trace)
        self.assertIn("~ScopedCounter() noexcept { --counter; }", trace)
        self.assertIn("ModuleLifetime() noexcept : ScopedCounter<ModuleCounter>(get_module_lock())", trace)
        self.assertIn("class Trace : private ModuleLifetime, public std::enable_shared_from_this<Trace>", trace)
        self.assertIn("defined(WINRT_NO_MODULE_LOCK) || defined(WINRT_CUSTOM_MODULE_LOCK)", trace)
        for forbidden in ("FreeLibrary", "LoadLibrary", "HMODULE"):
            self.assertNotIn(forbidden, trace)
        self.assertIn("accepted == EventLimit - 1", trace)
        self.assertIn("events[accepted++] = finish", trace)
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
        self.assertLess(writer.index("co_await resume_background()"), writer.index("if (self->folder.empty()) Initialize(self);"))
        self.assertEqual(writer.split("static fire_and_forget WriteReceipt(", 1)[0].count("co_await"), 1)
        self.assertIn("if (self->state.stop || !self->state.journal.Pop(event))", writer)
        self.assertIn("self->state.Fail(static_cast<std::uint32_t>(hr.value))", writer)
        enqueue_failure = trace.split('Failure(L"activation diagnostics enqueue"', 1)[1]
        self.assertIn("state.Fail(static_cast<std::uint32_t>(hr.value))", enqueue_failure)
        self.assertIn('Failure(L"activation diagnostics write", hr)', trace)
        self.assertNotIn("get_strong", trace)
        self.assertNotIn("BackgroundTaskDeferral", trace)
        for forbidden in ("JobTitle", "destination", "SourceContent", "source.oxps",
                          "Jobs", "Registrations", "init_apartment", "std::thread",
                          "StartDoc", "SetPrinter", "WaitForSingleObject"):
            self.assertNotIn(forbidden, trace)
        add = trace.split("void Add(", 1)[1].split("void Cancel(", 1)[0]
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
        baseline = original(OBSERVATION_BASELINE, "desktop-pdf/Task.cpp").decode()
        def block(text, start, end):
            return text.split(start, 1)[1].split(end, 1)[0]
        for start, end in (
            ("void ReleaseDeferral()", "fire_and_forget Finalize()"),
            ("auto uri = session.Printer()", "auto tickEnd = GetTickCount64()"),
            ("StorageFile target{nullptr};", "auto launchEnd = GetTickCount64()"),
            ("auto result = launch.GetResults();", "if (stop() || regCanceled) {"),
            ("if (!cancelSent) {", "if (mode == Mode::SaveAs"),
        ):
            self.assertEqual(block(task, start, end), block(baseline, start, end))
        self.assertIn("return stopSample.Evaluate", task)
        self.assertIn("if (stop() || regCanceled) {", task)
        for call in ("launch.Status()", "launch.Cancel()", "launch.GetResults()", "args.CompleteJob(status)"):
            self.assertEqual(task.count(call), baseline.count(call), call)
        launch = block(task, "auto launchEnd", "auto result = launch.GetResults();")
        self.assertLess(launch.index("launch.Cancel();"), launch.index('throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT)'))
        self.assertEqual(task.count("args.CompleteJob(status);"), 1)
        self.assertIn("auto deadline = GetTickCount64() + 45000;", task)
        self.assertIn("auto elected = gate.ClaimFinish(dataOwner);", task)
        self.assertIn("if (!elected) return;", task)
        self.assertIn("if (!gate.ClaimData())", task)
        self.assertIn("activation::FinalAttempt release", task)
        self.assertIn("auto reason = finalReason.load();", task)
        self.assertIn("co_await diagnostics->Finish(reason, deferral != nullptr, began)", task)
        self.assertEqual(task.count("deferral.Complete();"), 1)
        tail = '        if (primary.value < 0 && job) {'
        observed_tail = task.split(tail, 1)[1].split("struct Factory", 1)[0]
        for addition in (
            "        bool completeOwned = false;\n",
            "            if (diagnostics) completeOwned = diagnostics->Observe().CompleteEnter(\n"
            "                {activation::ObservationStamp(), static_cast<std::uint32_t>(status)});\n",
            "            if (diagnostics) diagnostics->Observe().CompleteReturn(completeOwned, {activation::ObservationStamp(), {}});\n",
            "            if (diagnostics) diagnostics->Observe().CompleteReturn(\n"
            "                completeOwned, {activation::ObservationStamp(), static_cast<std::uint32_t>(primary.value)});\n",
            "            try { if (diagnostics) co_await diagnostics->FlushBeforeComplete(); }\n"
            "            catch (...) { Failure(L\"journal pre-complete flush\", to_hresult()); }\n",
        ):
            self.assertEqual(observed_tail.count(addition), 1)
            observed_tail = observed_tail.replace(addition, "", 1)
        observed_tail = observed_tail.replace("primary = to_hresult();\n            Trace(",
                                              "primary = to_hresult(); Trace(", 1)
        self.assertEqual(observed_tail,
                         baseline.split(tail, 1)[1].split("struct Factory", 1)[0].replace(
                             "Finish(activation::Reason::Normal, true);", "Finish(activation::Reason::Normal, true, 4);", 1).replace(
                             'co_await Publish(job, L"completion.json", completion);',
                             'PublishNative(std::wstring(job.Path()), L"completion.json", completion);', 1))
        # 008: completion.json is written synchronously (Win32) so Finish seals right after CompleteJob.
        self.assertEqual(task.count("PublishNative("), 2)
        self.assertNotIn('Publish(job, L"completion.json"', task)
        native = task.split("inline void PublishNative(", 1)[1].split("\n}\n", 1)[0]
        self.assertNotIn("co_await", native)
        self.assertIn('io.Write(io.Create(pending), "\\xEF\\xBB\\xBF" + text);', native)
        self.assertIn('io.Rename(pending, path + L"\\\\" + std::wstring(name));', native)
        self.assertIn("primary = to_hresult(); Trace(activation::Point::CompleteFailure, primary);", observed_tail)
        self.assertIn("Failure(stage.c_str(), primary);", task)
        # 006: bounded journal flush strictly between CompleteBefore and CompleteJob entry.
        data_tail = task.split(tail, 1)[1].split("struct Factory", 1)[0]
        self.assertEqual(task.count("FlushBeforeComplete()"), 1)
        self.assertLess(data_tail.index("Trace(activation::Point::CompleteBefore);"),
                        data_tail.index("co_await diagnostics->FlushBeforeComplete();"))
        self.assertLess(data_tail.index("co_await diagnostics->FlushBeforeComplete();"),
                        data_tail.index("CompleteEnter("))
        self.assertLess(data_tail.index("CompleteEnter("), data_tail.index("args.CompleteJob(status);"))
        # 006: framework cancellation is post-complete teardown only after published CompleteJob entry.
        canceled = task.split("auto token = task.Canceled(", 1)[1].split("bool installed;", 1)[0]
        self.assertIn("teardown = context.origin == 0 && complete.entry.state == 2;", canceled)
        self.assertLess(canceled.index("auto complete = observations.ReadComplete();"),
                        canceled.index("teardown = context.origin == 0"))
        self.assertLess(canceled.index("context = observations.EnterCancel(context);"),
                        canceled.index("teardown = context.origin == 0"))
        self.assertIn("auto chosen = teardown ? activation::Reason::PostCompleteTeardown : activation::Reason::Canceled;",
                      canceled)
        self.assertIn("self->ObserveCancellation(context, chosen);", canceled)
        self.assertIn("self->Finish(chosen, false, 3);", canceled)
        self.assertEqual(task.count("PostCompleteTeardown"), 2)
        # 007: only post-complete teardown asks the journal to keep draining within the cancel budget.
        self.assertEqual(task.count(
            "diagnostics->Cancel(GetTickCount64(), context, reason == activation::Reason::PostCompleteTeardown);"), 1)
        self.assertEqual(task.count("ObserveCancellation(context);"), 1)
        # The SDK COM header declares this export without noexcept; body/factory stay exact.
        unload = 'extern "C" HRESULT __stdcall DllCanUnloadNow()'
        baseline_factory = baseline.split("struct Factory", 1)[1]
        self.assertEqual(task.split("struct Factory", 1)[1], baseline_factory)
        self.assertIn(unload, task)

    def test_journal_terminal_backend_and_ack_boundary(self):
        trace = (ROOT / "desktop-pdf/ActivationDiagnostics.h").read_text()
        backend = (ROOT / "desktop-pdf/ActivationJournalReport.h").read_text()
        self.assertIn("DrainMs = 1500, FinalizeMs = 2000, ReportMs = 500, CancelMs = 250", trace)
        self.assertIn("published = event.sequence", trace)
        publish = trace.split("static void PublishEvent(", 1)[1].split("static fire_and_forget Drain(", 1)[0]
        self.assertLess(publish.index("self->io->Create(staging)"), publish.index("self->io->Write(std::move(file), bytes)"))
        self.assertLess(publish.index("self->io->Write(std::move(file), bytes)"), publish.index("self->io->Rename("))
        self.assertLess(publish.index("self->io->Rename("), publish.index("self->state.Published(event)"))
        self.assertIn('auto bytes = "\\xEF\\xBB\\xBF" + to_string(record.Stringify());', publish)
        self.assertIn('self->folder + L"\\\\" + std::to_wstring(event.sequence) + L".json"', publish)
        self.assertIn("self->io->Folder(folder, false);", publish)
        self.assertEqual(publish.count("self->io->Folder(root, true);"), 1)
        self.assertEqual(publish.count("self->io->Folder(parent, true);"), 1)
        # 007: per-record writes are synchronous native Win32 on the background writer; no WinRT storage.
        io = trace.split("struct JournalIo {", 1)[1].split("\n};\n", 1)[0]
        self.assertIn("CreateDirectoryFromAppW(path.c_str(), nullptr)", io)
        self.assertIn("if (!openIfExists || error != ERROR_ALREADY_EXISTS)", io)
        self.assertIn("CreateFile2FromAppW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_NEW, &options)", io)
        self.assertIn("FILE_FLAG_OPEN_REPARSE_POINT", io)
        self.assertIn("WriteFile(file.get(), bytes.data()", io)
        self.assertIn("if (count != bytes.size())", io)
        self.assertIn("MoveFileFromAppW(from.c_str(), to.c_str())", io)
        self.assertIn("return TerminalCachePath();", io)
        for forbidden in ("FlushFileBuffers(", "MoveFileEx", "REPLACE_EXISTING", "DeleteFile", "co_await", "IAsync"):
            self.assertNotIn(forbidden, io)
        for forbidden in ("StorageFolder", "StorageFile", "FileIO::", "CreateFolderAsync", "CreateFileAsync",
                          "RenameAsync", "WriteTextAsync", "IAsyncInfo", "IAsyncOperation", "CancelOperation",
                          "CancelCurrent", "TrackedOperation", "Track(", "ApiEntry(", "cancelRequested"):
            self.assertNotIn(forbidden, trace)
        self.assertIn("inline constexpr std::uint64_t TeardownDrainMs = 125;", trace)
        self.assertIn("if (!failure) failure =", trace)
        self.assertIn("Require(!decision", trace)
        self.assertIn('{"deferral_release_observed", "false"}', trace)
        self.assertIn('{"version", Number(version, 5)}', trace)
        self.assertIn('{"cancel_requested", "false"}', trace)
        self.assertIn('if (version == 5) wire += std::string(",\\"cancellation_drain\\":")', trace)
        self.assertIn("PreCompleteFlushMs = 500, PreCompleteSliceMs = 25;", trace)
        self.assertIn("std::uint8_t version = 5) {", trace)
        self.assertIn("version == 2 ? 2300u : 3300u", trace)
        baseline = original(OBSERVATION_BASELINE, "desktop-pdf/ActivationDiagnostics.h").decode()
        state = trace.split("class PublicationState {", 1)[1].split("\ninline std::string ReceiptWire", 1)[0]
        cancel = state.split("void Cancel(std::uint64_t tick, bool postComplete = false) noexcept {", 1)[1].split("\n    }\n", 1)[0]
        self.assertIn("auto& cutoff = postComplete ? teardown : canceled;", cancel)
        self.assertNotIn("stop", cancel)
        self.assertIn("if (finalizing && (canceled || now >= DrainEnd(*finalizing))) stop = true;", state)
        self.assertIn("return teardown ? (std::min)(end, *teardown + TeardownDrainMs) : end;", state)
        self.assertIn("auto state = canceled ? DrainState::Canceled :", state)
        self.assertIn("!canceled && teardown.has_value(), deferral", state)
        finish = trace.split("IAsyncAction Finish(", 1)[1]
        old_finish = baseline.split("IAsyncAction Finish(", 1)[1]
        for old, new in (
            ("end = (std::min)(began + DrainMs, FinalizeDeadline(began, state.canceled));", "end = state.DrainEnd(began);"),
            ("FinalizeDeadline(began, state.canceled), deferral);",
             "FinalizeDeadline(began, state.Cutoff()), deferral);\n"
             "                    decision.observations.first = firstApplied;\n"
             "                    decision.observations.minimum = minimumApplied;\n"
             "                    decision.flush = preComplete;"),
            ("        CancelOperation();\n", ""),
            ("(std::min)(reportEnd, FinalizeDeadline(began, state.canceled));",
             "(std::min)(reportEnd, FinalizeDeadline(began, state.Cutoff()));"),
        ):
            self.assertEqual(old_finish.count(old), 1, old)
            old_finish = old_finish.replace(old, new, 1)
        self.assertEqual(finish, old_finish)
        self.assertIn("co_await resume_on_signal", trace)
        self.assertNotRegex(trace, r"\b(?:action|operation|launch)\.get\(")
        self.assertIn("CREATE_NEW", backend)
        self.assertIn("MoveFileFromAppW", backend)
        self.assertIn("FlushFileBuffers", backend)
        for forbidden in ("DeleteFile", "MoveFileEx", "REPLACE_EXISTING", "FileIO::", "SetSecurity"):
            self.assertNotIn(forbidden, backend)
        self.assertIn('L"\\\\DesktopPdfJournal-"', backend)
        for guard in ('L"Miyako.DesktopPdfIntegration"', 'L"CN=DesktopPdfPrototypeDev"'):
            self.assertIn(guard, backend)
        self.assertIn("PRIVATE OneCore", (ROOT / "desktop-pdf/CMakeLists.txt").read_text())
        self.assertIn("else result.secondaryCloseError = closeError;", backend)
        self.assertIn("ReportFailures(result,", trace)
        self.assertIn('L"journal terminal secondary close"', trace)

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
        manifest_bytes = (ROOT / "desktop-pdf/package/AppxManifest.xml").read_bytes()
        self.assertEqual(manifest_bytes.count(b'Version="0.1.0.8"'), 1)
        self.assertEqual(manifest_bytes.replace(b'Version="0.1.0.8"', b'Version="0.1.0.2"', 1),
                         original(JOURNAL_BASELINE, "desktop-pdf/package/AppxManifest.xml"))
        helper_bytes = (ROOT / "desktop-pdf/build.ps1").read_bytes()
        self.assertEqual(helper_bytes.count(b"'0.1.0.8'"), 1)
        def manifest_guard(raw):
            return raw.split(b"function Manifest(", 1)[1].split(b"function PackageEvidence(", 1)[0]
        self.assertEqual(manifest_guard(helper_bytes).replace(b"'0.1.0.8'", b"'0.1.0.2'", 1),
                         manifest_guard(original(JOURNAL_BASELINE, "desktop-pdf/build.ps1")))
        package = ET.parse(ROOT / "desktop-pdf/package/AppxManifest.xml").getroot()
        identity = package.find("m:Identity", NS)
        self.assertEqual(identity.attrib["Name"], "Miyako.DesktopPdfIntegration")
        self.assertEqual(identity.attrib["Publisher"], "CN=DesktopPdfPrototypeDev")
        self.assertEqual(identity.attrib["Version"], "0.1.0.8")
        apps = {app.attrib["Id"]: app for app in package.findall("m:Applications/m:Application", NS)}
        self.assertEqual(len(package.findall("m:Applications/m:Application", NS)), 2)
        self.assertEqual(set(apps), {"SaveAs", "Client"})
        app = apps["SaveAs"]
        self.assertEqual(app.attrib["Executable"], "IntegrationApp.exe")
        self.assertEqual(app.attrib["EntryPoint"], "DesktopPdfIntegration.App")
        self.assertEqual(app.attrib[f"{{{NS['u10']}}}SupportsMultipleInstances"], "true")
        extensions = app.findall("m:Extensions/psa:Extension", NS)
        self.assertEqual(len(extensions), 2)
        queues = {ext.find("psa:PrintSupportVirtualPrinter", NS).attrib["PrinterUri"]:
                  ext.find("psa:PrintSupportVirtualPrinter", NS) for ext in extensions}
        self.assertEqual(set(queues), {"desktop-pdf:save-as", "desktop-pdf:silent"})
        baseline = ET.fromstring(original(INTEGRATION_BASELINE, "desktop-pdf/package/AppxManifest.xml"))
        for app_id, uri in (("SaveAs", "desktop-pdf:save-as"), ("Silent", "desktop-pdf:silent")):
            queue = queues[uri]
            self.assertEqual(queue.attrib["PrinterUri"], uri)
            self.assertEqual(queue.attrib["PreferredInputFormat"], "application/oxps")
            self.assertEqual(queue.attrib.get("OutputFileTypes"), "pdf" if app_id == "SaveAs" else None)
            before = baseline.find(f"m:Applications/m:Application[@Id='{app_id}']/m:Extensions/psa:Extension", NS)
            after = next(ext for ext in extensions if ext.find("psa:PrintSupportVirtualPrinter", NS).attrib["PrinterUri"] == uri)
            self.assertEqual(ET.tostring(after), ET.tostring(before), "PSA extension must move unchanged")
        self.assertEqual(len(app.findall("m:Extensions/*", NS)), 3)
        hosts = package.findall("m:Applications/m:Application/m:Extensions/desktop:Extension", NS)
        self.assertEqual(len(hosts), 1)
        self.assertEqual(hosts[0].attrib, {
            "Category": "windows.fullTrustProcess", "Executable": "IntegrationHost.exe",
        })
        client = apps["Client"]
        self.assertEqual(ET.tostring(client), ET.tostring(baseline.find("m:Applications/m:Application[@Id='Client']", NS)))
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
        self.assertIn("$identity.Version -cne '0.1.0.8'", helper)
        self.assertIn("'Build', 'Package', 'SignCopy'", helper)
        self.assertIn("No automatic fetch/install.", helper)
        for ref in (DEPENDENCY, PROOF):
            self.assertIn(ref, helper)
        for forbidden in ("New-SelfSignedCertificate", "Import-Certificate", "Add-AppxPackage",
                          "Assert-PrinterPackage", "Assert-PrinterResourceDump", "printer/build.ps1",
                          "'/nv'", "Invoke-WebRequest", "git fetch"):
            self.assertNotIn(forbidden, helper)
        package_marker = "} else {\n            $build = if ($ValidationProfile -ceq 'isolated-journal-v3') {"
        self.assertEqual(helper.count(package_marker), 1)
        package = helper[helper.index(package_marker):]
        self.assertNotIn("'--build'", package)
        self.assertNotIn("Checked $ctest", package)
        self.assertIn("PackageEvidence $package $pri", package)
        self.assertIn("ConvertFrom-ScopedBuildReceipt ([IO.File]::ReadAllText", package)
        self.assertIn("Get-Content -LiteralPath (Join-Path $BuildDirectory 'own-build.json') -Raw | ConvertFrom-Json", package)

    def test_install_verifier_is_read_only(self):
        text = (ROOT / "desktop-pdf/tools/VerifyInstall.ps1").read_text()
        allowed = {"ConvertFrom-Json", "ConvertTo-Json", "ForEach-Object", "Get-AppxPackage", "Get-Content",
                   "Get-Item", "Get-PrintConfiguration", "Get-Printer", "Join-Path", "Set-StrictMode",
                   "Sort-Object", "Test-Path", "Where-Object"}
        used = set(re.findall(r"\b[A-Z][a-z]+-[A-Z][A-Za-z]+\b", text))
        self.assertEqual(used - allowed, set())
        for forbidden in ("SetValue", "DeleteValue", "CreateSubKey", "OpenSubKey($", "WriteAll", "Invoke-",
                          "Start-Process", "IntegrationClient.exe", "Out-File", "> "):
            self.assertNotIn(forbidden, text)
        self.assertIn("OpenSubKey('PrinterDriverData')", text)
        self.assertIn("V4_PDC_ChangeID", text)

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
