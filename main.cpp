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

int usage()
{
	std::cerr << "Usage: ShaderGen <source-file> <data-folder> [-g] Targets...\n";
	return -1;
}

int generateCPP(const std::wstring& fileName, const std::wstring& outFolder, const DxcBuffer& sourceBuffer, std::vector<LPCWSTR> arguments, const std::vector<std::wstring>& includes);
int transpileGLSL(const std::wstring& fileName, const std::wstring& outFolder, IDxcResult* result, const std::wstring& profile);
int transpileGLSLES(const std::wstring& fileName, const std::wstring& outFolder, IDxcResult* result, const std::wstring& profile);
int transpileHLSL(int apilevel, const std::wstring& fileName, const std::wstring& outFolder, IDxcResult* result, bool debug, const std::vector<std::wstring>& includes, const std::wstring& profile);
int transpileSPIRV(const std::wstring& fileName, const std::wstring& outFolder, std::vector<LPCWSTR> arguments, IDxcBlobEncoding* pSource, const std::wstring& profile, const std::wstring &entrypoint);

#if WINDOWS
#define ARGV_CMP(i, s) (wcscmp(argv[i], L#s) == 0)
#define CONVERT(s) s
#else
#define ARGV_CMP(i, s) (strcmp(argv[i], #s) == 0)
#define CONVERT(s) converter.from_bytes(s)
#endif

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
		else if (ARGV_CMP(i, -GLSL) || ARGV_CMP(i, -HLSL11) || ARGV_CMP(i, -HLSL12) || ARGV_CMP(i, -GLSLES) || ARGV_CMP(i, -SPIRV)) {
			shaderTarget = true;
		}
	}

	HRESULT hr = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
	CHECK_HR(DxcCreateInstance / Library);

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
	CHECK_HR(DxcCreateInstance / Compiler);

	hr = library->CreateDefaultIncludeHandler(&includeHandler);
	CHECK_HR(CreateIncludeHandler);

	ReleasePtr<IDxcBlobEncoding> pSource;
	hr = library->LoadFile(sourceFile.c_str(), nullptr, &pSource);
	CHECK_HR(CreateBlobFromFile);

	std::vector<LPCWSTR> arguments;

	arguments.push_back(sourceFile.c_str());

	arguments.push_back(L"-Zpc");

	arguments.push_back(L"-HV");
	arguments.push_back(L"2021");

	for (const std::wstring& include : includes) {
		arguments.push_back(L"-I");
		arguments.push_back(include.c_str());
	}

	DxcBuffer sourceBuffer;
	sourceBuffer.Ptr = pSource->GetBufferPointer();
	sourceBuffer.Size = pSource->GetBufferSize();
	sourceBuffer.Encoding = 0;

	ReleasePtr<IDxcResult> pCompileResult;

	std::vector<LPCWSTR> spirvArguments;
	if (!profile.empty()) {
		spirvArguments = arguments;


		spirvArguments.push_back(L"-E");
		spirvArguments.push_back(entrypoint.c_str());

		spirvArguments.push_back(L"-spirv");

		spirvArguments.push_back(L"-T");
		spirvArguments.push_back(profile.c_str());

		spirvArguments.push_back(L"-D");
		spirvArguments.push_back(L"export=");


		hr = compiler->Compile(&sourceBuffer, spirvArguments.data(), spirvArguments.size(), includeHandler, IID_PPV_ARGS(&pCompileResult));
		CHECK_HR(Compile);

		//Error Handling
		ReleasePtr<IDxcBlobUtf8> pErrors;
		hr = pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		CHECK_HR(GetOutput / Errors);
		if (pErrors && pErrors->GetStringLength() > 0) {
			std::cerr << "Error compiling HLSL" << std::endl;
			std::cerr << (char*)pErrors->GetBufferPointer() << std::endl;
			return -1;
		}
	}
	else if (shaderTarget) {
		std::cerr << "Profile needs to be specified to build shader Targets" << std::endl;
		return -1;
	}

	int result = 0;

	for (int i = 3; i < argc; ++i) {
		if (ARGV_CMP(i, -CPP)) {
			int cpp_result = generateCPP(sourceFile, L".", sourceBuffer, arguments, includes);
			if (cpp_result != 0)
				result = cpp_result;
		}
		else if (ARGV_CMP(i, -HLSL11)) {
			int hlsl_result = transpileHLSL(11, sourceFile, dataFolder, pCompileResult, debug, includes, profile);
			if (hlsl_result != 0)
				result = hlsl_result;
		}
		else if (ARGV_CMP(i, -HLSL12)) {
			int hlsl_result = transpileHLSL(12, sourceFile, dataFolder, pCompileResult, debug, includes, profile);
			if (hlsl_result != 0)
				result = hlsl_result;
		}
		else if (ARGV_CMP(i, -GLSL)) {
			int glsl_result = transpileGLSL(sourceFile, dataFolder, pCompileResult, profile);
			if (glsl_result != 0)
				result = glsl_result;
		}
		else if (ARGV_CMP(i, -GLSLES)) {
			int glsles_result = transpileGLSLES(sourceFile, dataFolder, pCompileResult, profile);
			if (glsles_result != 0)
				result = glsles_result;
		}
		else if (ARGV_CMP(i, -SPIRV)) {
			int spirv_result = transpileSPIRV(sourceFile, dataFolder, std::move(spirvArguments), pSource, profile, entrypoint);
			if (spirv_result != 0)
				result = spirv_result;
		}
		else if (ARGV_CMP(i, -I) || ARGV_CMP(i, -E) || ARGV_CMP(i, -T)) {
			++i;
		}
		else if (!ARGV_CMP(i, -g)) {
			std::wcerr << L"Unknown target language: " << argv[i] << std::endl;
			result = -1;
		}
	}

	return result;
}