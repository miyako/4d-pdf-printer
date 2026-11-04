#pragma once
#include "../ActivationDiagnostics.h"
#include <thread>
#include <vector>

namespace pdf::tests {
inline void JournalChecks() {
    using namespace activation;
    Journal journal;
    Event event; event.point = Point::RunEnter; event.error = 0x80070057u;
    for (std::size_t i = 0; i != EventLimit - 1; ++i) Require(journal.Push(event), "31 ordinary slots");
    Require(!journal.Push(event) && journal.RejectedBefore() == 1, "reserved finish slot rejects overflow");
    Event finish; finish.point = Point::Finish;
    journal.Seal(finish); journal.Seal(finish);
    Require(journal.Accepted() == EventLimit, "exactly one reserved finish");
    Require(!journal.Push(event) && journal.RejectedAfter() == 1, "sealed journal");
    for (std::size_t i = 0; i != EventLimit; ++i) {
        Require(journal.Pop(event) && event.sequence == i, "contiguous FIFO");
        Require(i == EventLimit - 1 ? event.point == Point::Finish : event.error == 0x80070057u,
            "finish and original HRESULT");
    }
    Require(!journal.Pop(event), "no dequeue retry");
    Journal invalid;
    event.point = Point::Count;
    Require(!invalid.Push(event), "invalid checkpoint rejected");
    event.point = Point::Finish;
    Require(!invalid.Push(event), "finish only via seal");
    for (auto stage : StageNames) Require(!stage.empty(), "fixed stage names");
    PublicationState state;
    event.point = Point::JobCreated;
    Require(state.journal.Push(event), "accept event");
    Require(state.journal.Pop(event), "consume event");
    state.inflight = event; state.writing = true; state.stage = Stage::Write;
    Require(!state.published && !state.Drained(), "dequeue is not published");
    state.journal.Seal(finish);
    auto timeout = state.Freeze(Reason::Normal, 100, 1600, 2100, true);
    Require(timeout.state == DrainState::Deadline && !timeout.published && timeout.outstanding &&
        timeout.inflight->sequence == 0 && !timeout.failure && state.stop, "honest pending deadline");
    state.Published(event);
    Require(!state.decision->published && state.published == 0u, "late publication cannot upgrade decision");
    bool twice = false;
    try { state.Freeze(Reason::Normal, 100, 1601, 2100, true); } catch (...) { twice = true; }
    Require(twice, "one immutable decision");
    for (auto stage : {Stage::Initialize, Stage::Root, Stage::Parent, Stage::Folder, Stage::Prepare,
                       Stage::Create, Stage::Write, Stage::Rename, Stage::Enqueue}) {
        PublicationState failed; failed.stage = stage; failed.inflight = event;
        failed.Fail(0x80070005u); failed.stage = Stage::Rename; failed.Fail(0x80004005u);
        auto d = failed.Freeze(Reason::ActivationError, 10, 11, 2010, false);
        Require(d.state == DrainState::Failed && d.failure->stage == stage &&
            d.failure->code == 0x80070005u && !d.deferral && failed.stop, "first raw failure retained");
    }
    PublicationState clean;
    clean.journal.Seal(finish);
    Require(clean.journal.Pop(event) && !clean.Drained(), "empty dequeue not ack");
    clean.Published(event);
    Require(clean.Drained() && clean.Freeze(Reason::Normal, 1, 2, 2001, true).state == DrainState::Drained,
        "drained through finish");
    PublicationState full;
    event.point = Point::RunEnter;
    for (std::size_t i = 0; i != EventLimit - 1; ++i) full.journal.Push(event);
    full.journal.Seal(finish);
    while (full.journal.Pop(event)) full.Published(event);
    Require(full.Drained() && full.published == 31u, "exact 32 including finish is valid");
    PublicationState loss;
    event.point = Point::RunEnter;
    for (std::size_t i = 0; i != EventLimit; ++i) loss.journal.Push(event);
    loss.journal.Seal(finish);
    while (loss.journal.Pop(event)) loss.Published(event);
    Require(!loss.Drained() && loss.Freeze(Reason::Normal, 1, 2, 2001, true).state == DrainState::Failed,
        "published overflow is not clean coverage");
    PublicationState canceled;
    canceled.Cancel(100); canceled.Cancel(200); canceled.Cancel(99);
    Require(FinalizeDeadline(10, std::nullopt) == 2010 && FinalizeDeadline(1000, 100) == 350 &&
        FinalizeDeadline(10, 5000) == 2010 && canceled.canceled == 99ull, "absolute nonextending budgets");
    Require(canceled.Freeze(Reason::Canceled, 1000, 1000, 349, true).state == DrainState::Canceled,
        "already expired cancellation is not a new wait");
    PublicationState late;
    late.finalizing = 100;
    Require(late.MayIssue(1599) && !late.MayIssue(1600) && late.stop && !late.failure,
        "issue guard enforces original drain deadline without inventing I/O error");
    PublicationState canceledDuringDrain;
    canceledDuringDrain.finalizing = 100;
    canceledDuringDrain.Cancel(120);
    Require(!canceledDuringDrain.MayIssue(121), "cancellation stops new stage reservations");
    PublicationState teardown;
    teardown.finalizing = 100;
    teardown.Cancel(120, true); teardown.Cancel(130, true);
    Require(!teardown.canceled && teardown.teardown == 120ull && teardown.Cutoff() == 120ull &&
        teardown.DrainEnd(100) == 120 + TeardownDrainMs && FinalizeDeadline(100, teardown.Cutoff()) == 120 + CancelMs &&
        teardown.MayIssue(121) && teardown.MayIssue(120 + TeardownDrainMs - 1) && !teardown.stop,
        "post-complete teardown keeps issuing inside its share of the cancel budget");
    Require(!teardown.MayIssue(120 + TeardownDrainMs) && teardown.stop && !teardown.failure,
        "post-complete teardown drain stops at its budget without inventing I/O error");
    PublicationState lateSeal;
    lateSeal.Cancel(120, true);
    lateSeal.finalizing = 245;
    Require(ReceiptReserveMs == 60 && TeardownDrainMs + ReceiptReserveMs == CancelMs &&
        lateSeal.DrainEnd(245) == 120 + CancelMs - ReceiptReserveMs && lateSeal.MayIssue(245 + 15) && !lateSeal.stop,
        "teardown drain stays open for a finish sealed 125 ms after the cancel (0.1.0.7 device runs 02/03)");
    PublicationState continued;
    continued.Cancel(50, true);
    continued.journal.Seal(finish);
    Require(continued.journal.Pop(event), "teardown finish dequeued");
    continued.Published(event);
    auto drained = continued.Freeze(Reason::PostCompleteTeardown, 60, 61, 300, false);
    Require(drained.state == DrainState::Drained && drained.continued, "drained post-complete teardown is continued, not canceled");
    PublicationState both;
    both.Cancel(80, true); both.Cancel(90);
    Require(both.Cutoff() == 80ull && both.Freeze(Reason::Canceled, 100, 100, 330, false).state == DrainState::Canceled &&
        !both.decision->continued, "real cancellation always stops, even after teardown");
    for (bool dataOwner : {false, true}) {
        TaskGate gate;
        if (dataOwner) Require(gate.ClaimData() && !gate.ClaimFinish(false), "active Data retains ownership");
        std::atomic<unsigned> winners{};
        std::vector<std::thread> threads;
        for (int i = 0; i != 16; ++i) threads.emplace_back([&] { if (gate.ClaimFinish(dataOwner)) ++winners; });
        for (auto& thread : threads) thread.join();
        Require(winners == 1 && gate.Closing() && !gate.ClaimData(), "one concurrent finalizer");
        gate.Closed();
        Require(!gate.ClaimFinish(dataOwner), "no second finalizer");
    }
    TaskGate raceGate;
    Subscription<int> token;
    Require(raceGate.ClaimFinish(false), "synchronous subscribe reentry elected finalizer");
    Require(!token.Install(42, raceGate.Closing()) && !token.Take(), "late token must be revoked by caller");
    Subscription<int> installed;
    Require(installed.Install(7, false) && installed.Take() == 7 && !installed.Take(), "token taken once");
    unsigned complete = 0;
    try {
        FinalAttempt release([&]() noexcept { ++complete; });
        throw std::runtime_error("injected finalizer failure");
    } catch (std::runtime_error const&) {}
    Require(complete == 1, "deferral attempt survives unwinding");
}
struct FakeReport {
    activation::ReportStage fail{activation::ReportStage::None};
    bool shortWrite{};
    std::uint32_t error{5}, closeError{};
    std::vector<activation::ReportStage> calls;
    std::uint32_t Call(activation::ReportStage stage) {
        calls.push_back(stage); return stage == fail ? error : 0u;
    }
    std::uint32_t Create() { return Call(activation::ReportStage::Create); }
    std::uint32_t Write(std::string_view bytes, std::uint32_t& written) {
        written = static_cast<std::uint32_t>(bytes.size() - (shortWrite ? 1 : 0));
        return Call(activation::ReportStage::Write);
    }
    std::uint32_t Flush() { return Call(activation::ReportStage::Flush); }
    std::uint32_t Close() {
        auto result = Call(activation::ReportStage::Close);
        return result ? result : closeError;
    }
    std::uint32_t Rename() { return Call(activation::ReportStage::Rename); }
};
inline void ReportChecks() {
    using namespace activation;
    for (auto error : {5u, 112u}) for (auto failure : {ReportStage::None, ReportStage::Create, ReportStage::Write,
                         ReportStage::Flush, ReportStage::Close, ReportStage::Rename}) {
        FakeReport backend; backend.fail = failure; backend.error = error;
        auto result = PublishTerminal(backend, R"({"test":true})");
        Require(result.Published() == (failure == ReportStage::None), "terminal publication result");
        if (failure != ReportStage::None)
            Require(result.stage == failure && result.error == error, "exact access-denied/disk-full stage/code");
        Require(result.secondaryCloseError == 0, "primary close failure is not duplicated as secondary");
        unsigned reported = 0;
        ReportFailures(result, [&](ReportStage stage, std::uint32_t code, bool secondary) {
            ++reported;
            Require(stage == failure && code == error && !secondary, "primary fault reported once without secondary");
        });
        Require(reported == (failure == ReportStage::None ? 0u : 1u), "success emits no failure report");
        Require(std::count(backend.calls.begin(), backend.calls.end(), ReportStage::Create) == 1 &&
            std::count(backend.calls.begin(), backend.calls.end(), ReportStage::Rename) <= 1,
            "single create/rename, no retry");
        if (failure != ReportStage::Create)
            Require(std::count(backend.calls.begin(), backend.calls.end(), ReportStage::Close) == 1,
                "close attempted once even after failure");
        else Require(std::count(backend.calls.begin(), backend.calls.end(), ReportStage::Close) == 0,
            "failed create never closes an unopened handle");
    }
    FakeReport shortWrite; shortWrite.shortWrite = true;
    auto shortResult = PublishTerminal(shortWrite, R"({"test":true})");
    Require(shortResult.stage == ReportStage::Write && shortResult.error == 29 &&
        shortWrite.calls.size() == 3 && shortWrite.calls.back() == ReportStage::Close, "short write retained");
    for (auto stage : {ReportStage::Write, ReportStage::Flush}) {
        FakeReport secondary; secondary.fail = stage; secondary.closeError = 112;
        auto first = PublishTerminal(secondary, R"({"test":true})");
        Require(first.stage == stage && first.error == 5 && first.secondaryCloseError == 112,
            "first error and secondary close error both retained");
        struct Observed { ReportStage stage; std::uint32_t code; bool secondary; };
        std::vector<Observed> reports;
        ReportFailures(first, [&](ReportStage reportedStage, std::uint32_t code, bool isSecondary) {
            reports.push_back({reportedStage, code, isSecondary});
        });
        Require(reports.size() == 2 && reports[0].stage == stage && reports[0].code == 5 &&
            !reports[0].secondary && reports[1].stage == ReportStage::Close && reports[1].code == 112 &&
            reports[1].secondary, "production reporting surface emits first and secondary faults separately");
        std::vector<ReportStage> expected{ReportStage::Create, ReportStage::Write};
        if (stage == ReportStage::Flush) expected.push_back(ReportStage::Flush);
        expected.push_back(ReportStage::Close);
        Require(secondary.calls == expected, "secondary fault causes no rename/retry or extra close");
    }
    for (auto invalid : {std::string("{} trailing"), std::string(4097, 'x')}) {
        FakeReport backend; bool rejected = false;
        try { PublishTerminal(backend, invalid); } catch (...) { rejected = true; }
        Require(rejected && backend.calls.empty(), "invalid/oversize receipt performs no filesystem operation");
    }
}
}
