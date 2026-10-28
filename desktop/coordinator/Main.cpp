#include "../common/Common.h"
#include "desktop-v1.h"
#include <rpcasync.h>
#include <aclapi.h>
#include <userenv.h>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <cmath>
#include <winrt/Windows.Storage.h>

using namespace desktop;

extern "C" void* __RPC_USER midl_user_allocate(size_t size) { return malloc(size); }
extern "C" void __RPC_USER midl_user_free(void* value) { free(value); }

namespace
{
    Identity owner;
    std::vector<uint8_t> expectedPackage;
    std::mutex lifecycle;
    auto lastCall = std::chrono::steady_clock::now();
    std::atomic<uint32_t> stallsStarted{0}, stallsCompleted{0};

    struct Job
    {
        std::wstring secret, lease, workflowHash;
        std::chrono::steady_clock::time_point expires;
        bool claimed = false, canceled = false, completed = false;
        uint32_t replays = 0;
        uint32_t canceledClaims = 0;
        uint64_t sourceBytes = 0;
    };
    std::map<std::wstring, Job> jobs;

    Identity Caller(RPC_BINDING_HANDLE binding)
    {
        RPC_CALL_ATTRIBUTES_V2_W attributes{};
        attributes.Version = 2;
        CheckRpc(RpcServerInqCallAttributesW(binding, &attributes));
        if (attributes.NullSession || attributes.AuthenticationService != RPC_C_AUTHN_WINNT ||
            attributes.AuthenticationLevel < RPC_C_AUTHN_LEVEL_PKT_PRIVACY ||
            attributes.IsClientLocal != rcclLocal)
            throw winrt::hresult_error(E_ACCESSDENIED);
        CheckRpc(RpcImpersonateClient(binding));
        struct Revert
        {
            ~Revert()
            {
                if (RpcRevertToSelf() != RPC_S_OK) std::terminate();
            }
        } revert;
        HANDLE token = nullptr;
        Check(OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) != FALSE);
        handle owned(token);
        auto identity = ReadIdentity(token);
        if (identity.user != owner.user || identity.session != owner.session ||
            !identity.impersonation || identity.level < SecurityImpersonation ||
            (identity.appContainer && identity.container != expectedPackage))
            throw winrt::hresult_error(E_ACCESSDENIED);
        return identity;
    }

    RPC_STATUS RPC_ENTRY Authorize(RPC_IF_HANDLE, void* context)
    {
        try { Caller(context); return RPC_S_OK; }
        catch (...) { return RPC_S_ACCESS_DENIED; }
    }

    JsonObject Request(Operation operation, JsonObject const& input, Identity const& caller)
    {
        bool workflow = operation == ClaimGate || operation == FinishGate || operation == WorkflowPing;
        if (workflow != caller.appContainer) throw winrt::hresult_error(E_ACCESSDENIED);
        auto result = Record(L"handoff-gate");
        result.Insert(L"userChecked", JsonValue::CreateBooleanValue(true));
        result.Insert(L"sessionChecked", JsonValue::CreateBooleanValue(true));
        result.Insert(L"packageChecked", JsonValue::CreateBooleanValue(workflow));
        if (operation == Ping || operation == WorkflowPing)
        {
            result.Insert(L"ready", JsonValue::CreateBooleanValue(true));
            result.Insert(L"stallsStarted", JsonValue::CreateNumberValue(stallsStarted.load()));
            result.Insert(L"stallsCompleted", JsonValue::CreateNumberValue(stallsCompleted.load()));
            return result;
        }
        auto now = std::chrono::steady_clock::now();
        if (operation == PrepareGate)
        {
            for (auto it = jobs.begin(); it != jobs.end();)
            {
                if (it->second.expires < now) it = jobs.erase(it);
                else ++it;
            }
            if (jobs.size() >= 8) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TOO_MANY_CMDS));
            auto token = RandomToken();
            Job job;
            job.secret = RandomToken();
            job.expires = now + std::chrono::seconds(120);
            Text(result, L"title", TitlePrefix + token);
            Text(result, L"statusSecret", job.secret);
            jobs.emplace(token, std::move(job));
            return result;
        }
        auto title = std::wstring(input.GetNamedString(L"title"));
        if (!IsTitle(title)) throw winrt::hresult_invalid_argument();
        auto found = jobs.find(title.substr(std::size(TitlePrefix) - 1));
        if (found == jobs.end()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        auto& job = found->second;
        if (now >= job.expires) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        if (operation == ClaimGate)
        {
            if (input.GetNamedString(L"endpoint") != AutomationUri) throw winrt::hresult_invalid_argument();
            if (job.canceled)
            {
                ++job.canceledClaims;
                throw winrt::hresult_canceled();
            }
            if (job.claimed)
            {
                ++job.replays;
                throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
            }
            auto session = std::wstring(input.GetNamedString(L"workflowSession"));
            if (session.empty() || session.size() > 512) throw winrt::hresult_invalid_argument();
            job.workflowHash = Hash(session);
            job.lease = RandomToken();
            job.claimed = true;
            Text(result, L"lease", job.lease);
        }
        else if (operation == FinishGate)
        {
            if (!job.claimed || job.canceled || input.GetNamedString(L"lease") != job.lease)
                throw winrt::hresult_error(E_ACCESSDENIED);
            auto bytes = input.GetNamedNumber(L"sourceBytes");
            if (bytes <= 0 || bytes > 256.0 * 1024 * 1024) throw winrt::hresult_invalid_argument();
            if (!input.GetNamedBoolean(L"completeJobFailedReturned")) throw winrt::hresult_invalid_argument();
            job.sourceBytes = static_cast<uint64_t>(bytes);
            job.completed = true;
        }
        else
        {
            if (input.GetNamedString(L"statusSecret") != job.secret) throw winrt::hresult_error(E_ACCESSDENIED);
            if (operation == CancelGate) job.canceled = true;
            else if (operation != StatusGate) throw winrt::hresult_invalid_argument();
        }
        result.Insert(L"claimed", JsonValue::CreateBooleanValue(job.claimed));
        result.Insert(L"canceled", JsonValue::CreateBooleanValue(job.canceled));
        result.Insert(L"gateCompleted", JsonValue::CreateBooleanValue(job.completed));
        result.Insert(L"replaysRejected", JsonValue::CreateNumberValue(job.replays));
        result.Insert(L"canceledClaimsRejected", JsonValue::CreateNumberValue(job.canceledClaims));
        result.Insert(L"sourceBytes", JsonValue::CreateNumberValue(static_cast<double>(job.sourceBytes)));
        return result;
    }
}

extern "C" void DesktopGateRequest(PRPC_ASYNC_STATE async, handle_t binding, unsigned long operation,
    GateRequest* input, GateResponse* output)
{
    *output = {};
    try
    {
        auto caller = Caller(binding);
        auto end = std::find(std::begin(input->json), std::end(input->json), L'\0');
        if (end == std::end(input->json)) throw winrt::hresult_invalid_argument();
        auto request = JsonObject::Parse(winrt::hstring(input->json, static_cast<uint32_t>(end - input->json)));
        JsonObject result{nullptr};
        if (operation == StallGate || operation == WorkflowStallGate)
        {
            if (caller.appContainer != (operation == WorkflowStallGate))
                throw winrt::hresult_error(E_ACCESSDENIED);
            auto delay = request.GetNamedNumber(L"milliseconds");
            if (!std::isfinite(delay) || delay < 0 || delay > 5000 || delay != std::floor(delay))
                throw winrt::hresult_invalid_argument();
            ++stallsStarted;
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<DWORD>(delay)));
            ++stallsCompleted;
            result = Record(L"test-stall-completed");
        }
        else
        {
            std::scoped_lock guard(lifecycle);
            lastCall = std::chrono::steady_clock::now();
            result = Request(static_cast<Operation>(operation), request, caller);
        }
        auto text = result.Stringify();
        if (text.size() >= std::size(output->json)) throw winrt::hresult_error(E_BOUNDS);
        std::copy(text.begin(), text.end(), output->json);
    }
    catch (...)
    {
        auto error = static_cast<uint32_t>(winrt::to_hresult().value);
        output->code = (error & 0xffff0000u) == 0x80070000u ? error & 0xffffu : ERROR_INVALID_DATA;
    }
    if (RpcAsyncCompleteCall(async, nullptr) != RPC_S_OK)
        OutputDebugStringW(L"Desktop PDF gate RPC completion failed\n");
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring stage = L"arguments";
    try
    {
        if (argc != 2 || std::wstring(argv[1]) != L"--serve") throw winrt::hresult_invalid_argument();
        winrt::init_apartment();
        stage = L"identity";
        owner = CurrentIdentity();
        if (owner.appContainer || owner.session == 0) throw winrt::hresult_error(E_ACCESSDENIED);
        auto endpoint = Endpoint(owner);
        auto mutexName = L"Local\\" + endpoint;
        handle mutex(CreateMutexW(nullptr, FALSE, mutexName.c_str()));
        Check(mutex != nullptr);
        auto wait = WaitForSingleObject(mutex.get(), 0);
        if (wait == WAIT_TIMEOUT) return 0;
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) winrt::throw_last_error();
        auto family = winrt::Windows::ApplicationModel::Package::Current().Id().FamilyName();
        stage = L"package-sid";
        PSID packageSid = nullptr;
        winrt::check_hresult(DeriveAppContainerSidFromAppContainerName(family.c_str(), &packageSid));
        expectedPackage = CopySid(packageSid);
        LocalFree(packageSid);
        EXPLICIT_ACCESSW entries[2]{};
        for (auto& entry : entries)
        {
            entry.grfAccessPermissions = GENERIC_ALL;
            entry.grfAccessMode = SET_ACCESS;
            entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            entry.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
        }
        entries[0].Trustee.ptstrName = reinterpret_cast<LPWSTR>(owner.user.data());
        entries[1].Trustee.ptstrName = reinterpret_cast<LPWSTR>(expectedPackage.data());
        PACL acl = nullptr;
        CheckRpc(SetEntriesInAclW(2, entries, nullptr, &acl));
        std::unique_ptr<void, decltype(&LocalFree)> ownedAcl(acl, LocalFree);
        SECURITY_DESCRIPTOR descriptor{};
        Check(InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) != FALSE);
        Check(SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) != FALSE);
        stage = L"rpc-endpoint";
        CheckRpc(RpcServerUseProtseqEpW(reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncalrpc")),
            RPC_C_PROTSEQ_MAX_REQS_DEFAULT, reinterpret_cast<RPC_WSTR>(endpoint.data()), &descriptor));
        CheckRpc(RpcServerRegisterAuthInfoW(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr));
        stage = L"rpc-interface";
        CheckRpc(RpcServerRegisterIf3(DesktopGate_v1_0_s_ifspec, nullptr, nullptr,
            RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY | RPC_IF_ALLOW_SECURE_ONLY,
            8, 65536, Authorize, &descriptor));
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::scoped_lock guard(lifecycle);
            if (std::chrono::steady_clock::now() - lastCall > std::chrono::seconds(60)) break;
        }
        CheckRpc(RpcServerUnregisterIf(DesktopGate_v1_0_s_ifspec, nullptr, TRUE));
        Check(ReleaseMutex(mutex.get()) != FALSE);
        return 0;
    }
    catch (...)
    {
        auto code = winrt::to_hresult();
        try
        {
            using namespace winrt::Windows::Storage;
            auto folder = ApplicationData::Current().LocalFolder().CreateFolderAsync(
                L"Gate", CreationCollisionOption::OpenIfExists).get();
            auto file = folder.CreateFileAsync(RandomToken() + L".json", CreationCollisionOption::FailIfExists).get();
            auto record = Record(L"coordinator-startup-failed");
            Text(record, L"failureStage", stage);
            record.Insert(L"hresult", JsonValue::CreateNumberValue(code.value));
            FileIO::WriteTextAsync(file, record.Stringify()).get();
        }
        catch (...) { OutputDebugStringW(L"Desktop PDF gate coordinator evidence failed\n"); }
        return static_cast<int>(code.value);
    }
}
