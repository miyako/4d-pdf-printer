#pragma once
#include "../ActivationDiagnostics.h"
#include <thread>
#include <vector>

namespace pdf::tests {
inline activation::observation::Snapshot MaxObservation() {
    using namespace activation::observation;
    constexpr std::uint64_t n = 9007199254740991ull;
    constexpr auto p = UINT32_MAX;
    Snapshot s;
    Context framework{0, false, {n, p}, p};
    Context host{1, false, {n, p}, 0, 15, 12};
    s.cancellation = {Sample<Context>{2, false, framework}, Sample<Context>{2, false, host}};
    Context setter{3, false, {n, p}};
    s.reason = {2, false, Reason{setter, n, 3}};
    std::array<std::uint8_t, 5> requests{1, 3, 2, 4, 0};
    for (std::uint8_t c = 0; c != 5; ++c) {
        Finish f;
        f.entry = {n, p}; f.caller = c; f.requested = requests[c]; f.owner = c == 4;
        if (c != 4) { f.cas = 2; f.expected = std::uint8_t{4}; }
        s.attempts[c] = {2, false, f};
    }
    auto elected = *s.attempts[4].payload; elected.gate = true;
    s.winner = {2, false, elected}; s.began = {2, false, Stamp{n, p}};
    s.passed = {2, false, PassedReason{n, 3}};
    Operation op{std::uint16_t{65535}, 5, {}};
    for (std::size_t i = 0; i != 3; ++i) {
        ApiHead head{{n, p}, op, 1, {}};
        if (i == 2) head.launch = Stamp{n, p};
        s.api[i].head = {2, false, head};
        s.api[i].returned = {2, false, Outcome{{n, p}, p}};
        if (i != 2) s.api[i].awaited = {2, false, Outcome{{n, p}, p}};
    }
    return s;
}
inline std::string MaxObservationReceipt(std::uint8_t version = 2) {
    using namespace activation;
    constexpr std::uint64_t n = 9007199254740991ull;
    Decision d;
    d.reason = Reason::CallbackDeadline; d.state = DrainState::Canceled;
    d.sealed = d.tick = d.deadline = n; d.accepted = d.consumed = 32;
    d.rejectedBefore = d.rejectedAfter = UINT32_MAX;
    d.outstanding = true; d.stage = Stage::Folder;
    Event event; event.sequence = 31; event.point = Point::StartReturned;
    d.inflight = event; d.failure = DiagnosticFailure{Stage::Folder, UINT32_MAX, event};
    std::wstring pending = L"pending-" + std::wstring(32, L'f');
    std::copy(pending.begin(), pending.end(), d.pending.begin());
    observation::Context c{0, false, {n, UINT32_MAX}, UINT32_MAX};
    d.observations.first = observation::Applied{c, n}; d.observations.minimum = *d.observations.first;
    d.observations.operation = observation::Operation{std::uint16_t{65535}, 5, {}};
    if (version >= 4) {
        d.reason = Reason::PostCompleteTeardown;
        d.flush = PreCompleteFlush{FlushState::Deadline, 32, 32, n, n};
    }
    auto snapshot = MaxObservation();
    if (version == 5) {
        // v5 truth: continued teardown drain, no journal async operation or cancel route to observe.
        d.state = DrainState::Deadline; d.continued = true;
        d.observations.operation.reset();
        snapshot.api[0] = {}; snapshot.api[1] = {};
    }
    snapshot.complete.entry = {2, true, observation::CompleteEntry{{n, UINT32_MAX}, 2}};
    snapshot.complete.returned = {2, false, observation::Outcome{{n, UINT32_MAX}, UINT32_MAX}};
    snapshot.completeCancellation = {2, false, observation::CompleteCancellation{
        {n, UINT32_MAX}, {n, UINT32_MAX}, snapshot.complete}};
    return ReceiptWire(d, std::wstring(32, L'f'), std::wstring(32, L'f'),
        snapshot, UINT32_MAX, n, n, version);
}
inline std::string ObservationReceipt(bool bound, bool full = false) {
    using namespace activation;
    observation::Observations observations;
    auto attempt = observations.EnterFinish({{70, 9}, 4, 0, 0, true, true, false, {}});
    observations.FinishAttempt(attempt); observations.Began({80, 9}); observations.Passed({90, 0});
    PublicationState state;
    std::array points{Point::RunEnter, Point::DeferralAcquired, Point::TriggerBefore, Point::TriggerAcquired,
        Point::SessionBefore, Point::SessionSubscribed, Point::StartBefore, Point::StartReturned,
        Point::DataEnter, Point::EndpointChecked, Point::ContentChecked, Point::ConfigurationChecked,
        Point::SelectorChecked, Point::RegistrationChecked, Point::SubmissionChecked, Point::MetadataValidated,
        bound ? Point::JobCreated : Point::PreJobFailure, Point::CompleteBefore, Point::CompleteReturned};
    for (auto point : points) {
        Event event; event.point = point;
        if (point == Point::PreJobFailure) event.error = 0x80004005u;
        state.journal.Push(event);
    }
    if (full) for (unsigned i = 0; i != 12; ++i) {
        Event extra; extra.point = Point::SessionBefore;
        state.journal.Push(extra);
    }
    Event finish; finish.point = Point::Finish; state.journal.Seal(finish);
    Event event;
    while (state.journal.Pop(event)) state.Published(event);
    auto decision = state.Freeze(Reason::Normal, 100, 110, 2100, true);
    return ReceiptWire(decision, std::wstring(32, L'a'), bound ? std::wstring(32, L'b') : L"",
        observations.Read(), 100, 111, 112, 2);
}
inline void ObservationChecks() {
    using namespace activation;
    namespace o = observation;
    auto encoding = [](bool valid, auto action) {
        bool accepted = false;
        try { action(); accepted = true; } catch (std::exception const&) { if (valid) throw; }
        Require(accepted == valid, "exact production wire domain");
    };
    for (std::uint8_t caller = 0; caller != 5; ++caller)
        for (std::uint8_t cas = 0; cas != 3; ++cas)
            for (std::uint8_t expected = 0; expected != 6; ++expected)
                for (bool owner : {false, true}) for (bool gate : {false, true}) {
                    o::Finish f{{12, 3}, caller, std::array<std::uint8_t, 5>{1, 3, 2, 4, 0}[caller],
                        cas, owner, gate, false, {}};
                    if (expected) f.expected = static_cast<std::uint8_t>(expected - 1);
                    bool valid = owner == (caller == 4) &&
                        (caller == 4 ? cas == 0 && !f.expected :
                         (cas == 1 && caller != 3 && f.expected == 0) ||
                         (cas == 2 && f.expected && *f.expected >= 1 && *f.expected <= 4));
                    encoding(valid, [&] { o::Put(f); });
                }
    for (std::uint8_t state = 0; state != 4; ++state)
        for (bool repeated : {false, true}) for (bool payload : {false, true}) {
            o::Sample<o::Stamp> sample{state, repeated, {}};
            if (payload) sample.payload = o::Stamp{12, 3};
            bool valid = state <= 2 && (state == 2) == payload;
            encoding(valid, [&] { o::OnceWire(sample); });
            encoding(valid && (state != 0 || !repeated), [&] { o::FirstWire(sample); });
        }
    encoding(false, [&] { o::Number(9007199254740992ull); });
    encoding(false, [&] { o::Native(0); });
    encoding(false, [&] { o::Text("bad\\quote\""); });
    encoding(false, [&] { o::Put(o::Outcome{{12, 3}, std::uint32_t{0}}); });
    encoding(false, [&] { o::Put(o::Operation{std::uint16_t{0}, 0, {}}); });
    o::OperationKeys keys;
    for (unsigned i = 1; i <= 65535; ++i) Require(keys.Next() == i, "distinct checked operation key");
    Require(!keys.Next() && !keys.Next(), "operation key exhaustion is sticky unknown, never reuse");
    for (unsigned bits = 0; bits != 8; ++bits) {
        o::StopSample sample;
        unsigned calls = 0;
        bool value = sample.Evaluate([&] { ++calls; return (bits & 1) != 0; },
            [&] { ++calls; return (bits & 2) != 0; }, [&] { ++calls; return (bits & 4) != 0; });
        Require(value == (bits != 0) && calls == ((bits & 1) ? 1u : (bits & 2) ? 2u : 3u),
            "actual production stop helper preserves short circuit getter counts");
    }
    o::First<o::Stamp> first;
    Require(first.Claim(), "first observation claim");
    Require(!first.Claim(), "repeat does not overwrite publishing");
    auto partial = first.Read();
    Require(partial.state == 1 && partial.repeated && !partial.payload, "publishing remains unknown");
    first.Publish({12, 3});
    Require(first.Read().payload->tick == 12 && first.Read().repeated, "first immutable payload");
    for (unsigned iteration = 0; iteration != 64; ++iteration) {
        o::First<o::Stamp> cell;
        std::atomic<unsigned> winners{};
        std::atomic<bool> torn{};
        std::vector<std::thread> writers;
        std::thread reader([&] {
            for (unsigned sample = 0; sample != 1000; ++sample) {
                auto value = cell.Read();
                if ((value.repeated && value.state == 0) ||
                    (value.payload && value.payload->tick != value.payload->thread)) torn = true;
            }
        });
        for (unsigned i = 1; i != 9; ++i) writers.emplace_back([&, i] {
            if (cell.Claim()) { cell.Publish({i, i}); ++winners; }
        });
        for (auto& writer : writers) writer.join();
        reader.join();
        auto value = cell.Read();
        Require(!torn && winners == 1 && value.state == 2 && value.repeated &&
            value.payload->tick == value.payload->thread, "one ready untorn concurrent first");
    }
    o::Observations observations;
    auto framework = observations.EnterCancel({0, false, {10, 4}, 0xffffffffu});
    auto repeated = observations.EnterCancel({0, false, {11, 5}, 9});
    Require(framework.first && !repeated.first, "first raw reason distinct from later caller");
    observations.SetReason(repeated, 12, 4);
    auto finish = observations.EnterFinish({{13, 6}, 4, 0, 0, true, true, false, {}});
    observations.FinishAttempt(finish);
    observations.Began({14, 6}); observations.Passed({15, 4});
    o::Operation a{std::uint16_t{1}, 9, std::uint8_t{17}}, b{std::uint16_t{2}, 9, std::uint8_t{18}};
    Require(observations.ApiEntry(1, {{16, 7}, a, 11, {}}), "exact retained API head");
    observations.Await(b, 22, {{17, 8}, {}});
    Require(observations.Read().api[1].awaited.state == 0, "B cannot acknowledge A");
    observations.Await(a, 22, {{17, 8}, {}});
    Require(observations.Read().api[1].awaited.state == 0, "key alone cannot acknowledge A");
    observations.Await(a, 11, {{18, 8}, {}});
    observations.ApiReturn(1, true, {{19, 7}, {}});
    auto snapshot = observations.Read();
    Require(snapshot.reason.payload->context.raw == 9 && !snapshot.reason.payload->context.first &&
        snapshot.cancellation[0].payload->raw == 0xffffffffu, "actual setter does not borrow first context");
    Require(snapshot.winner.payload->caller == 4 && snapshot.passed.payload->value == 4,
        "gate owner and reason setter differ");
    Require(snapshot.api[1].awaited.payload->observed.tick == 18 &&
        snapshot.api[1].returned.payload->observed.tick == 19, "natural outcome and API return are separate");
    Require(MaxObservationReceipt().size() == 2295, "producer field-envelope emission below2300");
    Require(MaxObservationReceipt(3).size() <= 3300, "CompleteJob envelope below3300/backend4096");
    auto maximum4 = MaxObservationReceipt(4);
    Require(maximum4.size() <= 3300 && maximum4.size() < 4096 &&
        maximum4.find("\"finalize_reason\":\"post-complete-teardown\"") != std::string::npos &&
        maximum4.find("\"pre_complete_flush\":{\"state\":\"deadline\",\"target_count\":32,") != std::string::npos,
        "terminal v4 teardown/pre-complete flush envelope below3300/backend4096");
    auto maximum5 = MaxObservationReceipt(5);
    Require(maximum5.size() <= 3300 && maximum5.find("\"version\":5,") != std::string::npos &&
        maximum5.find("\"drain_state\":\"deadline\"") != std::string::npos &&
        maximum5.find("\"cancel_requested\":false") != std::string::npos &&
        maximum5.find("\"i\":null") != std::string::npos &&
        maximum5.ends_with(",\"cancellation_drain\":\"continued\"}"),
        "terminal v5 continued teardown envelope below3300/backend4096");
    {
        o::Context origin0{0, false, {12, 3}, 7}, origin1{1, false, {12, 3}, 0, 9, 1};
        encoding(true, [&] { o::Put(o::Reason{origin0, 13, 5}, true); });
        encoding(true, [&] { o::Put(o::Reason{origin0, 13, 4}, true); });
        encoding(false, [&] { o::Put(o::Reason{origin0, 13, 5}); });
        encoding(false, [&] { o::Put(o::Reason{origin1, 13, 5}, true); });
        for (std::uint8_t caller = 0; caller != 5; ++caller)
            for (std::uint8_t requested = 0; requested != 7; ++requested)
                for (std::uint8_t expected = 1; expected != 7; ++expected) for (bool teardown : {false, true}) {
                    o::Finish f{{12, 3}, caller, requested, 2, caller == 4, false, false, expected};
                    if (caller == 4) { f.cas = 0; f.expected.reset(); }
                    auto base = std::array<std::uint8_t, 5>{1, 3, 2, 4, 0}[caller];
                    auto top = teardown ? 5 : 4;
                    bool valid = (requested == base || (teardown && caller == 3 && requested == 5)) &&
                        (caller == 4 || expected <= top);
                    encoding(valid, [&] { o::Put(f, teardown); });
                }
        encoding(true, [&] { o::Put(o::PassedReason{12, 5}, true); });
        encoding(false, [&] { o::Put(o::PassedReason{12, 5}); });
        Decision d;
        d.reason = Reason::PostCompleteTeardown; d.state = DrainState::Deadline;
        auto receipt = [&](std::uint8_t version) {
            return ReceiptWire(d, std::wstring(32, L'a'), L"", o::Snapshot{}, 100, 111, 112, version);
        };
        encoding(false, [&] { receipt(3); });
        encoding(false, [&] { receipt(2); });
        encoding(false, [&] { receipt(6); });
        Require(receipt(5).ends_with(",\"cancellation_drain\":\"none\"}"), "v5 teardown race without provenance");
        d.continued = true;
        encoding(false, [&] { receipt(5); });
        encoding(false, [&] { receipt(4); });
        o::Context applied{0, false, {12, 3}, 7};
        d.observations.first = o::Applied{applied, 13}; d.observations.minimum = d.observations.first;
        Require(receipt(5).ends_with(",\"cancellation_drain\":\"continued\"}"), "v5 continued teardown drain");
        encoding(false, [&] { receipt(4); });
        d.state = DrainState::Canceled;
        encoding(false, [&] { receipt(5); });
        d.continued = false;
        Require(receipt(5).ends_with(",\"cancellation_drain\":\"stopped\"}"), "v5 stopped canceled drain");
        d.state = DrainState::Drained;
        encoding(false, [&] { receipt(5); });
        d.state = DrainState::Deadline; d.observations = {};
        Require(receipt(4).find("\"pre_complete_flush\":{\"state\":\"not-attempted\",\"target_count\":null,"
            "\"published_count\":null,\"begin_tick_ms\":null,\"end_tick_ms\":null}}") != std::string::npos,
            "default terminal v4 unattempted flush");
        d.reason = Reason::Canceled;
        d.flush = PreCompleteFlush{FlushState::Flushed, 3, 2, 10, 11};
        encoding(false, [&] { receipt(4); });
        d.flush = PreCompleteFlush{FlushState::Deadline, 3, 2, 12, 11};
        encoding(false, [&] { receipt(4); });
        d.flush = PreCompleteFlush{FlushState::InProgress, 3, 0, 10, 0};
        Require(receipt(4).find("\"state\":\"in-progress\",\"target_count\":3,\"published_count\":null,"
            "\"begin_tick_ms\":10,\"end_tick_ms\":null") != std::string::npos, "in-progress flush has no end");
        encoding(false, [&] { receipt(3); });
        d.flush = PreCompleteFlush{FlushState::Flushed, 3, 3, 10, 11};
        Require(receipt(4).find("\"state\":\"flushed\",\"target_count\":3,\"published_count\":3") !=
            std::string::npos && receipt(4).size() < 4096, "flushed terminal v4");
    }
    o::Observations complete;
    Require(complete.ReadComplete().entry.state == 0, "CompleteJob not attempted at read");
    auto owned = complete.CompleteEnter({{20, 7}, 0});
    Require(owned && complete.ReadComplete().returned.state == 0, "entry alone is not in-progress proof");
    auto context = complete.EnterCancel({0, false, {21, 8}, 7});
    auto atCancel = complete.ReadComplete();
    complete.CompleteCanceled(context, atCancel, {22, 8});
    complete.CompleteReturn(owned, {{23, 7}, {}});
    auto completeSnapshot = complete.Read();
    Require(completeSnapshot.complete.returned.state == 2 &&
        completeSnapshot.completeCancellation.payload->call.returned.state == 0,
        "framework sample stays immutable after return");
    Require(!complete.CompleteEnter({{24, 9}, 2}), "duplicate completion cannot steal outcome");
    complete.CompleteReturn(false, {{25, 9}, 0x80004005u});
    Require(complete.ReadComplete().entry.repeated &&
        !complete.ReadComplete().returned.payload->error, "repeated ambiguity retains first result");
    o::Observations failed;
    auto failedOwner = failed.CompleteEnter({{20, 7}, 2});
    failed.CompleteReturn(failedOwner, {{21, 7}, 0x80070005u});
    Require(failed.ReadComplete().returned.payload->error == 0x80070005u, "original CompleteJob HRESULT");
    Require(ObservationReceipt(false).find("\"job\":\"\"") != std::string::npos,
        "pre-job business failure can emit unbound Normal/drained activation receipt");
    for (auto [mask, value] : {std::pair{9, 1}, {9, 9}, {11, 2}, {11, 10}, {15, 4}, {15, 8}, {15, 12}})
        Require(o::HostMask(static_cast<std::uint8_t>(mask), static_cast<std::uint8_t>(value)), "source short circuit mask");
}
}
