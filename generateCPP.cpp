#define NOMINMAX
#ifdef _WIN32
#    include <Windows.h>
#endif
#include <dxc/dxcapi.h>

#include <iostream>

#include "releaseptr.h"

#include <spirv_cross.hpp>

#include <codecvt>

#include <fstream>

#include <locale>

#include <map>

#include <optional>

#include "dxc/Support/D3DReflection.h"

extern ReleasePtr<IDxcUtils> library;
extern ReleasePtr<IDxcCompiler3> compiler;
extern ReleasePtr<IDxcIncludeHandler> includeHandler;

struct FunctionSignature {

	std::string name;
	std::string returnType;
	std::vector<std::string> parameterTypes;

};

#define check(c) if (*s++ != c) {--s; return {};}

std::string demangleType(const char*& s, std::vector<std::string>& types, std::vector<std::string>& names);

std::string demangleTemplateType(const char*& s, std::vector<std::string>& types, std::vector<std::string>& names) {
	if (*s != '$') {
		return demangleType(s, types, names);
	}
	++s;
	switch (*s++) {
	case '0':
		return std::string{ static_cast<char>(*s++ + 1) };
	default:
		--s;
		return {};
	}
}

std::optional<std::vector<std::string>> demangleTypeList(const char*& s, std::vector<std::string>& names) {
	std::vector<std::string> result;
	if (*s == 'X') {
		++s;
		return result;
	}
	while (*s != '@') {
		std::string type = demangleTemplateType(s, result, names);
		if (type.empty())
			return {};
		result.push_back(type);
	}
	++s;
	return result;
}

std::string demangleNameFragmentImpl(const char*& s, std::vector<std::string>& names) {
	bool isComplex = *s == '?' && *(s + 1) == '$';
	if (isComplex)
		s += 2;
	const char* end = strchr(s, '@');
	std::string result = { s, end };
	s = end + 1;
	if (isComplex) {
		std::optional<std::vector<std::string>> parameters = demangleTypeList(s, names);
		if (!parameters)
			return {};
		bool first = true;
		result += '<';
		for (std::string parameter : *parameters) {
			if (first)
				first = false;
			else
				result += ", ";
			result += parameter;
		}
		result += '>';
	}
	return result;
}

std::string demangleNameFragment(const char*& s, std::vector<std::string>& names) {
	if (*s >= '0' && *s <= '9') {
		return names[*s++ - '0'];
	}
	std::string name = demangleNameFragmentImpl(s, names);
	if (!name.empty()) {
		names.push_back(name);
	}
	return name;
}

std::string demangleTypeImpl(const char*& s, std::vector<std::string>& names) {
	switch (*s++) {
	case 'X':
		return "void";
	case 'M':
		return "float";
	case '?':
		switch (*s++) {
		case 'A':
			return demangleTypeImpl(s, names);
		default:
			--s;
			return {};
		}
		break;
	case 'V':
	case 'U': {
		std::string result;
		while (*s != '@') {
			std::string name = demangleNameFragment(s, names);
			if (name.empty())
				return {};
			result += name;
		}
		++s;
		return result;
	}
	default:
		--s;
		return {};
	}
}

std::string demangleType(const char*& s, std::vector<std::string>& types, std::vector<std::string>& names) {
	if (*s >= '0' && *s <= '9') {
		return types[*s++ - '0'];
	}
	return demangleTypeImpl(s, names);
}

std::string demangleQualifiedName(const char*& s, std::vector<std::string>& names) {
	std::string name = demangleNameFragment(s, names);
	if (name.empty()) {
		return {};
	}
	while (*s != '@') {
		std::string type = demangleNameFragment(s, names);
		if (type.empty())
			return {};
		name = type + "::" + name;
	}
	++s;
	return name;
}

std::optional<FunctionSignature> demangle(const char*& s) {


	std::vector<std::string> names;

	FunctionSignature result;

	check('\01');
	check('?');
	result.name = demangleQualifiedName(s, names);
	check('Y');
	check('A');

	std::vector<std::string> types;
	result.returnType = demangleType(s, types, names);
	if (result.returnType.empty())
		return {};

	std::optional<std::vector<std::string>> argumentTypes = demangleTypeList(s, names);
	if (!argumentTypes)
		return {};

	result.parameterTypes = *argumentTypes;

	check('Z');

	return result;
}

void forwardDeclare(std::ostream& o, std::string type) {
	if (type.find('<') == std::string::npos && type != "float") {
		o << "struct " << type << ";\n";
	}
}

std::string patchType(std::string s) {
	size_t i = s.find("vector<float, ");
	while (i != std::string::npos) {
		s.replace(i, i + 14, "Engine::Vector");
		s.erase(i + 15);
		i = s.find("vector<float, ");
	}
	return s;
}

int generateCPP(const std::wstring& _filePath, const std::wstring& outFolder, const DxcBuffer& sourceBuffer, std::vector<LPCWSTR> arguments, const std::vector<std::wstring>& includes, std::map<std::wstring, std::vector<std::wstring>>& profileEntrypoints) {

	std::cout << "CPP ... ";

	arguments.push_back(L"-Vd");
	arguments.push_back(L"-Od");

	arguments.push_back(L"-T");
	arguments.push_back(L"lib_6_6");

	arguments.push_back(L"-Wno-ignored-attributes");

	ReleasePtr<IDxcResult> pCompileResult;
	HRESULT compileError = compiler->Compile(&sourceBuffer, arguments.data(), arguments.size(), includeHandler, IID_PPV_ARGS(&pCompileResult));

	//Error Handling
	ReleasePtr<IDxcBlobUtf8> pErrors;
	HRESULT hr = pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
	CHECK_HR(GetOutput / Errors);
	if (pErrors && pErrors->GetStringLength() > 0) {
		std::cerr << "Error compiling HLSL" << std::endl;
		std::cerr << (char*)pErrors->GetBufferPointer() << std::endl;		
	}

	if (FAILED(compileError)) {
		std::cerr << "Failed to compile HLSL shader." << std::endl;
		return -1;
	}


	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

	std::string filePath = converter.to_bytes(_filePath);

	std::string fileName = filePath.substr(filePath.rfind('/') + 1);
	std::string baseName = fileName.substr(0, fileName.rfind('.'));

	ReleasePtr<IDxcBlob> pSpirv;
	hr = pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&pSpirv), nullptr);

	if (SUCCEEDED(hr)) {
		ReleasePtr<ID3D12LibraryReflection> reflection;

		DxcBuffer buffer;
		buffer.Ptr = pSpirv->GetBufferPointer();
		buffer.Size = pSpirv->GetBufferSize();
		buffer.Encoding = DXC_CP_ACP;

		hr = library->CreateReflection(&buffer, IID_PPV_ARGS(&reflection));
		CHECK_HR(Reflect);

		D3D12_LIBRARY_DESC desc;
		hr = reflection->GetDesc(&desc);
		CHECK_HR(Reflect / GetDesc);

		std::ofstream of{ converter.to_bytes(outFolder) + '/' + baseName + "_hlsl.h" };

		of << "#pragma once\n";

		of << R"(
    #include "Madgine/render/shaderfileobject.h"

)";

		of << "namespace HLSL {\n";

		of << "    inline const Engine::Render::ShaderMetadata file_" << baseName << "{\n\
        \"" << filePath << "\",\n\
        {\n";
		bool first = true;
		for (const std::wstring& include : includes) {
			if (first)
				first = false;
			else
				of << ", \n";
			of << "            \"" << converter.to_bytes(include) << "\"";
		}
		of << "\n        }\n\
    };\n\n";

		for (UINT i = 0; i < desc.FunctionCount; ++i) {
			ID3D12FunctionReflection* function = reflection->GetFunctionByIndex(i);
			D3D12_FUNCTION_DESC functionDesc;
			hr = function->GetDesc(&functionDesc);
			CHECK_HR(Reflect / Function / GetDesc);

			const char* s = functionDesc.Name;
			std::optional<FunctionSignature> signature = demangle(s);

			std::string suffix = signature->name.substr(signature->name.size() - 2);
			if (suffix == "VS") {
				profileEntrypoints[L"vs_6_2"].push_back(converter.from_bytes(signature->name));
			}
			else if (suffix == "PS") {
				profileEntrypoints[L"ps_6_2"].push_back(converter.from_bytes(signature->name));
			}

			if (!signature) {
				std::cerr << "Error demangling: " << s << "\n";
				return -1;
			}

			forwardDeclare(of, signature->returnType);
			for (std::string param : signature->parameterTypes) {
				forwardDeclare(of, param);
			}

			of << "    inline Engine::Render::ShaderFileObject<" << patchType(signature->returnType);

			for (std::string param : signature->parameterTypes) {
				of << ", " << patchType(param);
			}
			of << "> " << signature->name << " { file_" << baseName << ", \"" << signature->name << "\", {} };\n";
		}

		of << "}\n";
	}

	std::cout << "Success!" << std::endl;

	return 0;
}