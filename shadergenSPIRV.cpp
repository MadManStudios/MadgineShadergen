#define NOMINMAX
#ifdef _WIN32
#    include <Windows.h>
#endif
#include <dxc/dxcapi.h>

#include <spirv_glsl.hpp>

#include <vector>

#include <memory>

#include <iostream>

#include <map>

#include <fstream>

#include <codecvt>

#include <locale>

#include "releaseptr.h"

extern ReleasePtr<IDxcCompiler3> compiler;
extern ReleasePtr<IDxcIncludeHandler> includeHandler;

int transpileSPIRV(const std::wstring& fileName, const std::wstring &outFile, std::vector<LPCWSTR> arguments, IDxcBlobEncoding *pSource, const std::wstring& profile, const std::wstring& entrypoint)
{
    std::cout << "SPIRV... ";

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b0");
    arguments.push_back(L"0");
    arguments.push_back(L"0");
    arguments.push_back(L"0");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b1");
    arguments.push_back(L"0");
    arguments.push_back(L"1");
    arguments.push_back(L"0");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b2");
    arguments.push_back(L"0");
    arguments.push_back(L"2");
    arguments.push_back(L"0");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t0");
    arguments.push_back(L"0");
    arguments.push_back(L"0");
    arguments.push_back(L"1");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t0");
    arguments.push_back(L"1");
    arguments.push_back(L"0");
    arguments.push_back(L"2");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t1");
    arguments.push_back(L"1");
    arguments.push_back(L"1");
    arguments.push_back(L"2");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t0");
    arguments.push_back(L"2");
    arguments.push_back(L"0");
    arguments.push_back(L"3");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t1");
    arguments.push_back(L"2");
    arguments.push_back(L"1");
    arguments.push_back(L"3");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t2");
    arguments.push_back(L"2");
    arguments.push_back(L"2");
    arguments.push_back(L"3");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t0");
    arguments.push_back(L"3");
    arguments.push_back(L"0");
    arguments.push_back(L"4");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t1");
    arguments.push_back(L"3");
    arguments.push_back(L"1");
    arguments.push_back(L"4");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"t2");
    arguments.push_back(L"3");
    arguments.push_back(L"2");
    arguments.push_back(L"4");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b0");
    arguments.push_back(L"0");
    arguments.push_back(L"0");
    arguments.push_back(L"1");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b0");
    arguments.push_back(L"1");
    arguments.push_back(L"0");
    arguments.push_back(L"2");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b1");
    arguments.push_back(L"1");
    arguments.push_back(L"1");
    arguments.push_back(L"2");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b0");
    arguments.push_back(L"2");
    arguments.push_back(L"0");
    arguments.push_back(L"3");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b1");
    arguments.push_back(L"2");
    arguments.push_back(L"1");
    arguments.push_back(L"3");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b2");
    arguments.push_back(L"2");
    arguments.push_back(L"2");
    arguments.push_back(L"3");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b0");
    arguments.push_back(L"3");
    arguments.push_back(L"0");
    arguments.push_back(L"4");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b1");
    arguments.push_back(L"3");
    arguments.push_back(L"1");
    arguments.push_back(L"4");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"b2");
    arguments.push_back(L"3");
    arguments.push_back(L"2");
    arguments.push_back(L"4");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"s0");
    arguments.push_back(L"0");
    arguments.push_back(L"0");
    arguments.push_back(L"6");

    arguments.push_back(L"-fvk-bind-register");
    arguments.push_back(L"s1");
    arguments.push_back(L"0");
    arguments.push_back(L"1");
    arguments.push_back(L"6");

    //arguments.push_back(L"-P");

    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = pSource->GetBufferPointer();
    sourceBuffer.Size = pSource->GetBufferSize();
    sourceBuffer.Encoding = 0;

    ReleasePtr<IDxcResult> pCompileResult;
    HRESULT hr = compiler->Compile(&sourceBuffer, arguments.data(), arguments.size(), includeHandler, IID_PPV_ARGS(&pCompileResult));
    CHECK_HR(Compile);

    //Error Handling
    ReleasePtr<IDxcBlobUtf8> pErrors;
    hr = pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
    CHECK_HR(GetOutput / Errors);
    if (pErrors && pErrors->GetStringLength() > 0) {
        std::cerr << (char *)pErrors->GetBufferPointer() << std::endl;
        return -1;
    }

    ReleasePtr<IDxcBlob> pSpirv;
    hr = pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pSpirv), nullptr);
    CHECK_HR(GetOutput / Spirv)

    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    std::ofstream of { converter.to_bytes( outFile ), std::ios::binary };
    of.write(static_cast<char *>(pSpirv->GetBufferPointer()), pSpirv->GetBufferSize());

    std::cout << "Success!" << std::endl;

    return 0;
}
