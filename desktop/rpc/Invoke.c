#include "desktop-v1.h"
#include <rpcasync.h>
#include <stdlib.h>
#include <string.h>

typedef struct PendingCall
{
    RPC_ASYNC_STATE async;
    RPC_BINDING_HANDLE binding;
    GateRequest input;
    GateResponse output;
    HANDLE event;
    PTP_WAIT wait;
    HMODULE module;
} PendingCall;

static volatile LONG pendingCalls = 0;

static void FreeCall(PendingCall* call)
{
    CloseThreadpoolWait(call->wait);
    CloseHandle(call->event);
    if (call->binding) RpcBindingFree(&call->binding);
    if (call->module) FreeLibrary(call->module);
    free(call);
    InterlockedDecrement(&pendingCalls);
}

static VOID CALLBACK ReapCall(PTP_CALLBACK_INSTANCE instance, PVOID context,
    PTP_WAIT wait, TP_WAIT_RESULT result)
{
    PendingCall* call = context;
    RPC_STATUS status;
    (void)wait;
    (void)result;
    ResetEvent(call->event);
    status = RpcAsyncCompleteCall(&call->async, NULL);
    if (status == ERROR_IO_PENDING)
    {
        SetThreadpoolWait(call->wait, call->event, NULL);
        return;
    }
    FreeLibraryWhenCallbackReturns(instance, call->module);
    call->module = NULL;
    FreeCall(call);
}

static unsigned long StartCall(PendingCall* call, unsigned long operation)
{
    unsigned long code = ERROR_SUCCESS;
    RpcTryExcept
    {
        DesktopGateRequest(&call->async, call->binding, operation, &call->input, &call->output);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        code = RpcExceptionCode();
    }
    RpcEndExcept
    return code;
}

unsigned long InvokeDesktopGate(handle_t binding, unsigned long operation,
    GateRequest* request, GateResponse* response)
{
    PendingCall* call;
    RPC_STATUS code;
    DWORD waited;
    if (InterlockedIncrement(&pendingCalls) > 8)
    {
        InterlockedDecrement(&pendingCalls);
        return ERROR_TOO_MANY_CMDS;
    }
    call = calloc(1, sizeof(*call));
    if (!call)
    {
        InterlockedDecrement(&pendingCalls);
        return ERROR_OUTOFMEMORY;
    }
    call->input = *request;
    call->event = CreateEventExW(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    call->wait = CreateThreadpoolWait(ReapCall, call, NULL);
    if (!call->event || !call->wait)
    {
        code = GetLastError();
        if (call->wait) CloseThreadpoolWait(call->wait);
        if (call->event) CloseHandle(call->event);
        free(call);
        InterlockedDecrement(&pendingCalls);
        return code;
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        (LPCWSTR)&pendingCalls, &call->module))
    {
        code = GetLastError();
        FreeCall(call);
        return code;
    }
    code = RpcBindingCopy(binding, &call->binding);
    if (code != ERROR_SUCCESS) { FreeCall(call); return code; }
    code = RpcAsyncInitializeHandle(&call->async, sizeof(call->async));
    if (code != ERROR_SUCCESS) { FreeCall(call); return code; }
    call->async.NotificationType = RpcNotificationTypeEvent;
    call->async.u.hEvent = call->event;
    code = StartCall(call, operation);
    if (code != ERROR_SUCCESS) { FreeCall(call); return code; }
    waited = WaitForSingleObjectEx(call->event, 2000, FALSE);
    if (waited != WAIT_OBJECT_0)
    {
        DWORD waitError = waited == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
        /* ncalrpc ignores RPC_C_OPT_CALL_TIMEOUT. Abort without retry; the
           server may already have committed, and owns its separate buffers. */
        code = RpcAsyncCancelCall(&call->async, TRUE);
        SetThreadpoolWait(call->wait, call->event, NULL);
        return code == ERROR_SUCCESS ? waitError : code;
    }
    code = RpcAsyncCompleteCall(&call->async, NULL);
    if (code == ERROR_IO_PENDING)
    {
        RpcAsyncCancelCall(&call->async, TRUE);
        SetThreadpoolWait(call->wait, call->event, NULL);
        return ERROR_TIMEOUT;
    }
    if (code == ERROR_SUCCESS) *response = call->output;
    FreeCall(call);
    return code;
}

unsigned long DesktopGatePendingCalls(void)
{
    return (unsigned long)InterlockedCompareExchange(&pendingCalls, 0, 0);
}
