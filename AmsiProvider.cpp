#include "stdafx.h"

using namespace Microsoft::WRL;

// Define a trace logging provider: 00604c86-2d25-46d6-b814-cd149bfdf0b3
TRACELOGGING_DEFINE_PROVIDER(g_traceLoggingProvider, "SampleAmsiProvider",
    (0x00604c86, 0x2d25, 0x46d6, 0xb8, 0x14, 0xcd, 0x14, 0x9b, 0xfd, 0xf0, 0xb3));

HMODULE g_currentModule;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_currentModule = module;
        DisableThreadLibraryCalls(module);
        TraceLoggingRegister(g_traceLoggingProvider);
        TraceLoggingWrite(g_traceLoggingProvider, "Loaded");
        Module<InProc>::GetModule().Create();
        break;

    case DLL_PROCESS_DETACH:
        Module<InProc>::GetModule().Terminate();
        TraceLoggingWrite(g_traceLoggingProvider, "Unloaded");
        TraceLoggingUnregister(g_traceLoggingProvider);
        break;
    }
    return TRUE;
}

#pragma region COM server boilerplate
HRESULT WINAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().Terminate() ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID FAR* ppv)
{
    return Module<InProc>::GetModule().GetClassObject(rclsid, riid, ppv);
}
#pragma endregion

// Simple RAII class to ensure memory is freed.
template<typename T>
class HeapMemPtr
{
public:
    HeapMemPtr() { }
    HeapMemPtr(const HeapMemPtr& other) = delete;
    HeapMemPtr(HeapMemPtr&& other) : p(other.p) { other.p = nullptr; }
    HeapMemPtr& operator=(const HeapMemPtr& other) = delete;
    HeapMemPtr& operator=(HeapMemPtr&& other) {
        auto t = p; p = other.p; other.p = t;
    }

    ~HeapMemPtr()
    {
        if (p) HeapFree(GetProcessHeap(), 0, p);
    }

    HRESULT Alloc(size_t size)
    {
        p = reinterpret_cast<T*>(HeapAlloc(GetProcessHeap(), 0, size));
        return p ? S_OK : E_OUTOFMEMORY;
    }

    T* Get() { return p; }
    operator bool() { return p != nullptr; }

private:
    T* p = nullptr;
};

class
    DECLSPEC_UUID("2E5D8A62-77F9-4F7B-A90C-2744820139B2")
    SampleAmsiProvider : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IAntimalwareProvider, FtmBase>
{
public:
    IFACEMETHOD(Scan)(_In_ IAmsiStream* stream, _Out_ AMSI_RESULT* result) override;
    IFACEMETHOD_(void, CloseSession)(_In_ ULONGLONG session) override;
    IFACEMETHOD(DisplayName)(_Outptr_ LPWSTR* displayName) override;

private:
    // We assign each Scan request a unique number for logging purposes.
    LONG m_requestNumber = 0;
};

template<typename T>
T GetFixedSizeAttribute(_In_ IAmsiStream* stream, _In_ AMSI_ATTRIBUTE attribute)
{
    T result;

    ULONG actualSize;
    if (SUCCEEDED(stream->GetAttribute(attribute, sizeof(T), reinterpret_cast<PBYTE>(&result), &actualSize)) &&
        actualSize == sizeof(T))
    {
        return result;
    }
    return T();
}

HeapMemPtr<wchar_t> GetStringAttribute(_In_ IAmsiStream* stream, _In_ AMSI_ATTRIBUTE attribute)
{
    HeapMemPtr<wchar_t> result;

    ULONG allocSize;
    ULONG actualSize;
    if (stream->GetAttribute(attribute, 0, nullptr, &allocSize) == E_NOT_SUFFICIENT_BUFFER &&
        SUCCEEDED(result.Alloc(allocSize)) &&
        SUCCEEDED(stream->GetAttribute(attribute, allocSize, reinterpret_cast<PBYTE>(result.Get()), &actualSize)) &&
        actualSize <= allocSize)
    {
        return result;
    }
    return HeapMemPtr<wchar_t>();
}

HRESULT GetContentBytes(_In_ IAmsiStream* stream, _In_ PBYTE contentAddress, _In_ ULONGLONG contentSize, _Out_ BYTE** poutputbytes)
{
	BYTE* outputbytes = NULL;
	HRESULT hr;
	__try
	{
		outputbytes = (BYTE*)malloc(contentSize);
		SecureZeroMemory(outputbytes, contentSize);

		if (contentAddress)
		{
			hr = memcpy_s(outputbytes, contentSize, contentAddress, contentSize);
		}
		else
		{
			ULONG readSize;
			HRESULT hr = stream->Read(0, (ULONG)contentSize, outputbytes, &readSize);
		}
		__leave;
	}
	__finally
	{
		if (!SUCCEEDED(hr) && NULL != outputbytes)
		{
			free(outputbytes);
		}
		else
		{
			*poutputbytes = outputbytes;
		}
	}

	return hr;
}


HRESULT SampleAmsiProvider::Scan(_In_ IAmsiStream* stream, _Out_ AMSI_RESULT* result)
{
    LONG requestNumber = InterlockedIncrement(&m_requestNumber);
    TraceLoggingWrite(g_traceLoggingProvider, "Scan Start", TraceLoggingValue(requestNumber));
    BYTE* outputbytes = NULL;

    auto appName = GetStringAttribute(stream, AMSI_ATTRIBUTE_APP_NAME);
    auto contentName = GetStringAttribute(stream, AMSI_ATTRIBUTE_CONTENT_NAME);
    auto contentSize = GetFixedSizeAttribute<ULONGLONG>(stream, AMSI_ATTRIBUTE_CONTENT_SIZE);
    auto session = GetFixedSizeAttribute<ULONGLONG>(stream, AMSI_ATTRIBUTE_SESSION);
    auto contentAddress = GetFixedSizeAttribute<PBYTE>(stream, AMSI_ATTRIBUTE_CONTENT_ADDRESS);

    TraceLoggingWrite(g_traceLoggingProvider, "Attributes",
        TraceLoggingValue(requestNumber),
        TraceLoggingWideString(appName.Get(), "App Name"),
        TraceLoggingWideString(contentName.Get(), "Content Name"),
        TraceLoggingUInt64(contentSize, "Content Size"),
        TraceLoggingUInt64(session, "Session"),
        TraceLoggingPointer(contentAddress, "Content Address"));


	HRESULT hr = GetContentBytes(stream, contentAddress, contentSize, &outputbytes);
	if (!SUCCEEDED(hr))
	{
		TraceLoggingWrite(g_traceLoggingProvider, "Reading stream failed", TraceLoggingValue(requestNumber));
		// Still return S_OK, etc, just to make sure we don't cause issues
		*result = AMSI_RESULT_NOT_DETECTED;
		TraceLoggingWrite(g_traceLoggingProvider, "Scan End with error", TraceLoggingValue(requestNumber));
		return S_OK;
	}

	// We are only going to log any printable characters
	// NOTE: There may be other bytes in the stream with data, but this will do
	// the jobs for now
	CHAR* outputstring = (CHAR*)malloc(contentSize);
	SecureZeroMemory(outputstring, contentSize);
	ULONGLONG iString = 0;
	for (ULONGLONG i = 0; i < contentSize; i++)
	{
		if (outputbytes[i] >= 0x20 && outputbytes[i] <= 0x7e)
		{
			outputstring[iString] = outputbytes[i];
			iString++;
		}
	}
	TraceLoggingWrite(g_traceLoggingProvider, "Read All",
		TraceLoggingValue(requestNumber),
		TraceLoggingString(outputstring, "Data"));

	// Free anything allocated
	free(outputstring);
	free(outputbytes);

	// AMSI_RESULT_NOT_DETECTED means "We did not detect a problem but let other providers scan it, too."
	// Always make sure we return this, otherwise we risk making the system less secure
	*result = AMSI_RESULT_NOT_DETECTED;

    TraceLoggingWrite(g_traceLoggingProvider, "Scan End", TraceLoggingValue(requestNumber));
    return S_OK;
}

void SampleAmsiProvider::CloseSession(_In_ ULONGLONG session)
{
    TraceLoggingWrite(g_traceLoggingProvider, "Close session",
        TraceLoggingValue(session));
}

HRESULT SampleAmsiProvider::DisplayName(_Outptr_ LPWSTR *displayName)
{
    *displayName = const_cast<LPWSTR>(L"Sample AMSI Provider");
    return S_OK;
}

CoCreatableClass(SampleAmsiProvider);

#pragma region Install / uninstall

HRESULT SetKeyStringValue(_In_ HKEY key, _In_opt_ PCWSTR subkey, _In_opt_ PCWSTR valueName, _In_ PCWSTR stringValue)
{
    LONG status = RegSetKeyValue(key, subkey, valueName, REG_SZ, stringValue, (DWORD)(wcslen(stringValue) + 1) * sizeof(wchar_t));
    return HRESULT_FROM_WIN32(status);
}

STDAPI DllRegisterServer()
{
    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileName(g_currentModule, modulePath, ARRAYSIZE(modulePath)) >= ARRAYSIZE(modulePath))
    {
        return E_UNEXPECTED;
    }

    // Create a standard COM registration for our CLSID.
    // The class must be registered as "Both" threading model
    // and support multithreaded access.
    wchar_t clsidString[40];
    if (StringFromGUID2(__uuidof(SampleAmsiProvider), clsidString, ARRAYSIZE(clsidString)) == 0)
    {
        return E_UNEXPECTED;
    }

    wchar_t keyPath[200];
    HRESULT hr = StringCchPrintf(keyPath, ARRAYSIZE(keyPath), L"Software\\Classes\\CLSID\\%ls", clsidString);
    if (FAILED(hr)) return hr;

    hr = SetKeyStringValue(HKEY_LOCAL_MACHINE, keyPath, nullptr, L"SampleAmsiProvider");
    if (FAILED(hr)) return hr;

    hr = StringCchPrintf(keyPath, ARRAYSIZE(keyPath), L"Software\\Classes\\CLSID\\%ls\\InProcServer32", clsidString);
    if (FAILED(hr)) return hr;

    hr = SetKeyStringValue(HKEY_LOCAL_MACHINE, keyPath, nullptr, modulePath);
    if (FAILED(hr)) return hr;

    hr = SetKeyStringValue(HKEY_LOCAL_MACHINE, keyPath, L"ThreadingModel", L"Both");
    if (FAILED(hr)) return hr;

    // Register this CLSID as an anti-malware provider.
    hr = StringCchPrintf(keyPath, ARRAYSIZE(keyPath), L"Software\\Microsoft\\AMSI\\Providers\\%ls", clsidString);
    if (FAILED(hr)) return hr;

    hr = SetKeyStringValue(HKEY_LOCAL_MACHINE, keyPath, nullptr, L"SampleAmsiProvider");
    if (FAILED(hr)) return hr;

    return S_OK;
}

STDAPI DllUnregisterServer()
{
    wchar_t clsidString[40];
    if (StringFromGUID2(__uuidof(SampleAmsiProvider), clsidString, ARRAYSIZE(clsidString)) == 0)
    {
        return E_UNEXPECTED;
    }

    // Unregister this CLSID as an anti-malware provider.
    wchar_t keyPath[200];
    HRESULT hr = StringCchPrintf(keyPath, ARRAYSIZE(keyPath), L"Software\\Microsoft\\AMSI\\Providers\\%ls", clsidString);
    if (FAILED(hr)) return hr;
    LONG status = RegDeleteTree(HKEY_LOCAL_MACHINE, keyPath);
    if (status != NO_ERROR && status != ERROR_PATH_NOT_FOUND) return HRESULT_FROM_WIN32(status);

    // Unregister this CLSID as a COM server.
    hr = StringCchPrintf(keyPath, ARRAYSIZE(keyPath), L"Software\\Classes\\CLSID\\%ls", clsidString);
    if (FAILED(hr)) return hr;
    status = RegDeleteTree(HKEY_LOCAL_MACHINE, keyPath);
    if (status != NO_ERROR && status != ERROR_PATH_NOT_FOUND) return HRESULT_FROM_WIN32(status);

    return S_OK;
}
#pragma endregion
