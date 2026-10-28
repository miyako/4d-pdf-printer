#include "desktop-v1.h"

unsigned long InvokeDesktopGate(handle_t binding, unsigned long operation,
    GateRequest* request, GateResponse* response)
{
    unsigned long code = RPC_S_OK;
    RpcTryExcept
    {
        DesktopGateRequest(binding, operation, request, response);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        code = RpcExceptionCode();
    }
    RpcEndExcept
    return code;
}
