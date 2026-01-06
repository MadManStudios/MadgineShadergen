#define NOMINMAX
#ifdef _WIN32
#    include <Windows.h>
#endif
#include <dxc/dxcapi.h>

#include <vector>

#include <memory>

#include <assert.h>

#include <iostream>

#include <map>

#include <fstream>

#include <codecvt>

#include <sstream>

#include <locale>

#include "releaseptr.h"

ReleasePtr<IDxcUtils> library;
ReleasePtr<IDxcCompiler3> compiler;
ReleasePtr<IDxcIncludeHandler> includeHandler;

class IncludeHandler : public IDxcIncludeHandler {

	volatile std::atomic<ULONG> m_dwRef = { 0 };
public:
	ULONG STDMETHODCALLTYPE AddRef() noexcept override {

		return (ULONG)++m_dwRef;
	}
	ULONG STDMETHODCALLTYPE Release() override {

		ULONG result = (ULONG)--m_dwRef;
		if (result == 0) {
			delete this;
		}
		return result;
	}

	ReleasePtr<IDxcIncludeHandler> defaultIncludeHandler;

	IncludeHandler(ReleasePtr<IDxcIncludeHandler> defaultIncludeHandler) : m_dwRef(0), defaultIncludeHandler(std::move(defaultIncludeHandler)) {
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject) override {
		if (IsEqualIID(iid, __uuidof(IDxcIncludeHandler))) {
			*(IDxcIncludeHandler**)ppvObject = this;
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE LoadSource(
		_In_ LPCWSTR pFilename,                   // Filename as written in #include statement
		_COM_Outptr_ IDxcBlob** ppIncludeSource   // Resultant source object for included file
	) override {
		HRESULT hr = defaultIncludeHandler->LoadSource(pFilename, ppIncludeSource);
		if (SUCCEEDED(hr) && ppIncludeSource && *ppIncludeSource) {
			IDxcBlob* blob = *ppIncludeSource;

			constexpr const char prefix[] = R"(
#pragma once
#ifndef __keep
	#ifdef export
		#define __keep 1
	#else
		#define __keep 0
	#endif
#endif
#if __INCLUDE_LEVEL__ == 1
#define export 
#endif
#line 1
)";

			constexpr const char suffix[] = R"(
#if __INCLUDE_LEVEL__ == 1 && __keep == 0
#undef export
#endif
)";

			std::vector<std::byte> bytes;
			bytes.resize(sizeof(prefix) + sizeof(suffix) - 2 + blob->GetBufferSize());

			std::memcpy(bytes.data(), prefix, sizeof(prefix) - 1);
			std::memcpy(bytes.data() + sizeof(prefix) - 1, blob->GetBufferPointer(), blob->GetBufferSize());
			std::memcpy(bytes.data() + sizeof(prefix) - 1 + blob->GetBufferSize(), suffix, sizeof(suffix) - 1);

			IDxcBlobEncoding* outBlob = nullptr;

			blob->Release();

			hr = library->CreateBlob(bytes.data(), bytes.size(), 0, &outBlob);
			if (FAILED(hr)) {
				std::cerr << "Warum? " << hr << std::endl;
			}

			*ppIncludeSource = outBlob;
		}
		return hr;
	}
};

int usage()
{
	std::cerr << "Usage: ShaderGen <source-file> <data-folder> [-g] Targets...\n";
	return -1;
}

int generateCPP(const std::wstring& fileName, const std::wstring& outFile, const DxcBuffer& sourceBuffer, std::vector<LPCWSTR> arguments, std::vector<std::wstring> includes, std::map<std::wstring, std::vector<std::wstring>>& profileEntrypoints);
int transpileGLSL(const std::wstring& fileName, const std::wstring& outFile, IDxcResult* result, const std::wstring& profile);
int transpileGLSLES(const std::wstring& fileName, const std::wstring& outFile, IDxcResult* result, const std::wstring& profile);
int transpileHLSL(const std::wstring& fileName, const ReleasePtr<IDxcBlobEncoding>& pSource, const std::wstring& outFile, IDxcResult* result, bool debug, const std::vector<std::wstring>& includes, const std::wstring& profile);
int transpileSPIRV(const std::wstring& fileName, const std::wstring& outFile, std::vector<LPCWSTR> arguments, IDxcBlobEncoding* pSource, const std::wstring& profile, const std::wstring& entrypoint);

#if WINDOWS
#define ARGV_CMP(i, s) (wcscmp(argv[i], L#s) == 0)
#define CONVERT(s) s
#else
#define ARGV_CMP(i, s) (strcmp(argv[i], #s) == 0)
#define CONVERT(s) converter.from_bytes(s)
#endif

int transpile(int argc, char** argv, const std::wstring& profile, const std::wstring& entrypoint, const std::wstring& fileName, const std::vector<LPCWSTR>& arguments, const ReleasePtr<IDxcBlobEncoding>& pSource, bool debug, const std::vector<std::wstring>& includes, const std::wstring& dataFolder);

#if WINDOWS
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
	if (argc < 3) {
		return usage();
	}

	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

	std::wstring sourceFile = CONVERT(argv[1]);
	std::wstring dataFolder = CONVERT(argv[2]);

	std::vector<std::wstring> includes;

	std::wstring profile;
	std::wstring entrypoint = L"main";
	bool debug = false;
	bool shaderTarget = false;
	bool codegenTarget = false;
	for (int i = 3; i < argc; ++i) {
		if (ARGV_CMP(i, -g)) {
			debug = true;
		}
		else if (ARGV_CMP(i, -I)) {
			++i;
			if (i == argc)
				return usage();
			includes.push_back(CONVERT(argv[i]));
		}
		else if (ARGV_CMP(i, -T)) {
			++i;
			if (i == argc)
				return usage();
			profile = CONVERT(argv[i]);
		}
		else if (ARGV_CMP(i, -E)) {
			++i;
			if (i == argc)
				return usage();
			entrypoint = CONVERT(argv[i]);
		}
		else if (ARGV_CMP(i, -CPP)) {
			codegenTarget = true;
		}
		else if (ARGV_CMP(i, -GLSL) || ARGV_CMP(i, -HLSL) || ARGV_CMP(i, -GLSLES) || ARGV_CMP(i, -SPIRV)) {
			shaderTarget = true;
		}
	}

	if (shaderTarget && !codegenTarget && profile.empty()) {
		std::cerr << "Profile needs to be specified to build shader Targets without codegen" << std::endl;
		return -1;
	}

	HRESULT hr = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
	CHECK_HR(DxcCreateInstance / Library);

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
	CHECK_HR(DxcCreateInstance / Compiler);

	ReleasePtr<IDxcIncludeHandler> defaultHandler;
	hr = library->CreateDefaultIncludeHandler(&defaultHandler);
	CHECK_HR(CreateIncludeHandler);
	
	IncludeHandler* handler = new IncludeHandler(std::move(defaultHandler));
	handler->AddRef();
	includeHandler = ReleasePtr<IDxcIncludeHandler>(handler);

	ReleasePtr<IDxcBlobEncoding> pSource;
	hr = library->LoadFile(sourceFile.c_str(), nullptr, &pSource);
	if (FAILED(hr)) {
		std::wcerr << "Failed to load source file: " << sourceFile << ". HRESULT: " << hr << std::endl;
		return -1;
	}

	std::vector<LPCWSTR> arguments;

	arguments.push_back(sourceFile.c_str());

	arguments.push_back(L"-Zpc");

	arguments.push_back(L"-HV");
	arguments.push_back(L"2021");

	for (const std::wstring& include : includes) {
		arguments.push_back(L"-I");
		arguments.push_back(include.c_str());
	}

	std::map<std::wstring, std::vector<std::wstring>> profileEntrypoints;

	if (codegenTarget) {

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = pSource->GetBufferPointer();
		sourceBuffer.Size = pSource->GetBufferSize();
		sourceBuffer.Encoding = 0;


		int cpp_result = generateCPP(sourceFile, L".", sourceBuffer, arguments, includes, profileEntrypoints);
		if (cpp_result != 0)
			return cpp_result;
	}
	else if (shaderTarget) {
		profileEntrypoints[profile] = { entrypoint };
	}

	int result = 0;
	for (const auto& [profile, entrypoints] : profileEntrypoints) {
		for (const std::wstring& entrypoint : entrypoints) {
			int subResult = transpile(argc, argv, profile, entrypoint, sourceFile, arguments, pSource, debug, includes, dataFolder);
			if (subResult != 0 && result == 0)
				result = subResult;
		}
	}

	return result;
}

int transpile(int argc, char** argv, const std::wstring& profile, const std::wstring& entrypoint, const std::wstring& fileName, const std::vector<LPCWSTR>& arguments, const ReleasePtr<IDxcBlobEncoding>& pSource, bool debug, const std::vector<std::wstring>& includes, const std::wstring& dataFolder) {
	ReleasePtr<IDxcResult> pCompileResult;

	std::vector<LPCWSTR> spirvArguments;


	DxcBuffer sourceBuffer;
	sourceBuffer.Ptr = pSource->GetBufferPointer();
	sourceBuffer.Size = pSource->GetBufferSize();
	sourceBuffer.Encoding = 0;


	if (!profile.empty()) {
		spirvArguments = arguments;


		spirvArguments.push_back(L"-E");
		spirvArguments.push_back(entrypoint.c_str());

		spirvArguments.push_back(L"-spirv");

		spirvArguments.push_back(L"-T");
		spirvArguments.push_back(profile.c_str());

		spirvArguments.push_back(L"-D");
		spirvArguments.push_back(L"export=");


		HRESULT compileError = compiler->Compile(&sourceBuffer, spirvArguments.data(), spirvArguments.size(), includeHandler, IID_PPV_ARGS(&pCompileResult));

		//Error Handling
		ReleasePtr<IDxcBlobUtf8> pErrors;
		HRESULT hr = pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		CHECK_HR(GetOutput / Errors);
		if (pErrors && pErrors->GetStringLength() > 0) {
			std::cerr << "Error compiling HLSL" << std::endl;
			std::cerr << (char*)pErrors->GetBufferPointer() << std::endl;
		}

		if (FAILED(compileError)) {
			std::cerr << "Failed to compile HLSL to SPIR-V. HRESULT: " << compileError << std::endl;
			return -1;
		}
	}

	int result = 0;

	for (int i = 3; i < argc; ++i) {
		if (ARGV_CMP(i, -HLSL)) {
			int hlsl_result = transpileHLSL(fileName, pSource, dataFolder + L"/" + entrypoint + L"." + profile.substr(0, 2) + L"_hlsl", pCompileResult, debug, includes, profile);
			if (hlsl_result != 0)
				result = hlsl_result;
		}
		else if (ARGV_CMP(i, -GLSL)) {
			int glsl_result = transpileGLSL(fileName, dataFolder + L"/" + entrypoint + L".glsl", pCompileResult, profile);
			if (glsl_result != 0)
				result = glsl_result;
		}
		else if (ARGV_CMP(i, -GLSLES)) {
			int glsles_result = transpileGLSLES(fileName, dataFolder + L"/" + entrypoint + L".glsl_es", pCompileResult, profile);
			if (glsles_result != 0)
				result = glsles_result;
		}
		else if (ARGV_CMP(i, -SPIRV)) {
			int spirv_result = transpileSPIRV(fileName, dataFolder + L"/" + entrypoint + L".spirv", std::move(spirvArguments), pSource, profile, entrypoint);
			if (spirv_result != 0)
				result = spirv_result;
		}
		else if (ARGV_CMP(i, -I) || ARGV_CMP(i, -E) || ARGV_CMP(i, -T)) {
			++i;
		}
		else if (!ARGV_CMP(i, -g) && !ARGV_CMP(i, -CPP)) {
			std::wcerr << L"Unknown target language: " << argv[i] << std::endl;
			result = -1;
		}
	}

	return result;
}