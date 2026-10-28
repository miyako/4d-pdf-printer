#include "RpcClient.h"
#include "desktop-v1.h"
#include <thread>

extern "C" unsigned long InvokeDesktopGate(handle_t, unsigned long, GateRequest*, GateResponse*);
extern "C" void* __RPC_USER midl_user_allocate(size_t size) { return malloc(size); }
extern "C" void __RPC_USER midl_user_free(void* value) { free(value); }

namespace desktop
{
    JsonObject Call(Operation operation, JsonObject const& request)
    {
        auto identity = CurrentIdentity();
        auto endpoint = Endpoint(identity);
        RPC_WSTR raw = nullptr;
        CheckRpc(RpcStringBindingComposeW(nullptr, reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncalrpc")),
            nullptr, reinterpret_cast<RPC_WSTR>(endpoint.data()), nullptr, &raw));
        RPC_BINDING_HANDLE binding = nullptr;
        auto code = RpcBindingFromStringBindingW(raw, &binding);
        RpcStringFreeW(&raw);
        CheckRpc(code);
        struct BindingGuard
        {
            RPC_BINDING_HANDLE& value;
            ~BindingGuard() { RpcBindingFree(&value); }
        } guard{binding};
        RPC_SECURITY_QOS_V3_W qos{};
        qos.Version = RPC_C_SECURITY_QOS_VERSION_3;
        qos.Capabilities = RPC_C_QOS_CAPABILITIES_MUTUAL_AUTH | RPC_C_QOS_CAPABILITIES_LOCAL_MA_HINT;
        qos.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
        qos.ImpersonationType = RPC_C_IMP_LEVEL_IMPERSONATE;
        qos.Sid = identity.user.data();
        CheckRpc(RpcBindingSetAuthInfoExW(binding, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
            RPC_C_AUTHN_WINNT, nullptr, RPC_C_AUTHZ_NONE, reinterpret_cast<RPC_SECURITY_QOS*>(&qos)));
        CheckRpc(RpcBindingSetOption(binding, RPC_C_OPT_CALL_TIMEOUT, 2000));
        GateRequest input{};
        auto text = request.Stringify();
        if (text.size() >= std::size(input.json)) throw winrt::hresult_invalid_argument();
        std::copy(text.begin(), text.end(), input.json);
        GateResponse output{};
        CheckRpc(InvokeDesktopGate(binding, operation, &input, &output));
        CheckRpc(output.code);
        auto end = std::find(std::begin(output.json), std::end(output.json), L'\0');
        if (end == std::end(output.json)) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        return JsonObject::Parse(winrt::hstring(output.json, static_cast<uint32_t>(end - output.json)));
    }

    bool WaitReady(std::chrono::milliseconds timeout)
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        do
        {
            try
            {
                auto result = Call(CurrentIdentity().appContainer ? WorkflowPing : Ping);
                return result.GetNamedBoolean(L"ready");
            }
            catch (winrt::hresult_error const& error)
            {
                auto code = static_cast<uint32_t>(error.code().value);
                if (code != HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) &&
                    code != HRESULT_FROM_WIN32(RPC_S_SERVER_TOO_BUSY))
                    throw;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
}
