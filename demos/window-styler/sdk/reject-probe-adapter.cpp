#include "../adapter-api.h"
// Contract fixture: Automatic must continue after this read-only rejection.
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    return styler::ValidContext(c) ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *) {
    return ERROR_INVALID_DATA;
}
