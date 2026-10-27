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

static constexpr LPCWSTR sIndices[] = {
	L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8"
};

static constexpr LPCWSTR sBufferTypeIndices[] = {
	L"b0", L"b1", L"b2", L"b3"
};

static constexpr LPCWSTR sTextureTypeIndices[] = {
	L"t0", L"t1", L"t2", L"t3"
};

static constexpr LPCWSTR sSamplerTypeIndices[] = {
	L"s0", L"s1", L"s2", L"s3"
};




int transpileSPIRV(const std::wstring& fileName, const std::wstring& outFile, std::vector<LPCWSTR> arguments, IDxcBlobEncoding* pSource, const std::wstring& profile, const std::wstring& entrypoint)
{
	std::cout << "SPIRV... ";

	auto bind_register = [&](LPCWSTR typeIndex, size_t space, size_t binding, size_t set) {
		arguments.push_back(L"-fvk-bind-register");
		arguments.push_back(typeIndex);
		arguments.push_back(sIndices[space]);
		arguments.push_back(sIndices[binding]);
		arguments.push_back(sIndices[set]);
		};

	// Constant Buffers
	for (size_t i = 0; i < 4; ++i) {
		bind_register(sBufferTypeIndices[i], 0, i, 0);
	}

	// Memory Heap
	bind_register(sTextureTypeIndices[0], 0, 0, 1);

	// Instance Data
	bind_register(sTextureTypeIndices[0], 1, 0, 2);

	// Samplers
	for (size_t i = 0; i < 2; ++i) {
		bind_register(sSamplerTypeIndices[i], 0, i, 3);
	}

	// Resource Blocks
	for (size_t i = 0; i < 3; ++i) {
		for (size_t j = 0; j < 4; ++j) {
			bind_register(sTextureTypeIndices[j], 2 + i, j, 4 + i);
			bind_register(sBufferTypeIndices[j], 2 + i, j, 4 + i);
		}
	}


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
		std::cerr << (char*)pErrors->GetBufferPointer() << std::endl;
		return -1;
	}

	ReleasePtr<IDxcBlob> pSpirv;
	hr = pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pSpirv), nullptr);
	CHECK_HR(GetOutput / Spirv)

		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	std::ofstream of{ converter.to_bytes(outFile), std::ios::binary };
	of.write(static_cast<char*>(pSpirv->GetBufferPointer()), pSpirv->GetBufferSize());

	std::cout << "Success!" << std::endl;

	return 0;
}
