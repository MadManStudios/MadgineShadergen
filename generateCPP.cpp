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

#include <set>

#include <optional>

#include <sstream>

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

void dummy(std::ostream& out, size_t count = 1) {
	static size_t i = 0;
	for (size_t j = 0; j < count; ++j) {
		out << "float _dummy" << i++ << ";\n";
	}
}

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

int generateStruct(ID3D12ShaderReflectionType* type, UINT& size, std::ostream& of, std::map<std::string, UINT>& generatedStructs);

int writeType(ID3D12ShaderReflectionType* type, const char* name, UINT& size, std::ostream& target, std::ostream& of, std::map<std::string, UINT>& generatedStructs) {

	D3D12_SHADER_TYPE_DESC typeDesc;
	HRESULT hr = type->GetDesc(&typeDesc);
	CHECK_HR(ConstantBuffer / Type / GetDesc);

	std::string typeName = typeDesc.Name;

	switch (typeDesc.Class) {
	case D3D_SVC_SCALAR:
		size = 4;
		if (typeDesc.Type == D3D_SVT_BOOL)
			typeName = "uint32_t";
		break;
	case D3D_SVC_STRUCT:
		if (int result = generateStruct(type, size, of, generatedStructs))
			return result;
		break;
	case D3D_SVC_VECTOR:
		typeName = "Engine::Vector" + std::to_string(typeDesc.Columns);
		if (typeDesc.Type == D3D_SVT_INT)
			typeName += "i";
		size = 4 * typeDesc.Columns;
		break;
	case D3D_SVC_MATRIX_ROWS:
	case D3D_SVC_MATRIX_COLUMNS:
		assert(typeDesc.Columns == typeDesc.Rows);
		typeName = "Engine::Matrix" + std::to_string(typeDesc.Columns);
		size = 4 * typeDesc.Columns * typeDesc.Rows;
		break;
	default:
		std::cerr << "Unsupported type class: " << typeDesc.Class << std::endl;
		return -1;
	}


	target << "    " << typeName << " " << name;

	if (typeDesc.Elements > 0) {
		target << "[" + std::to_string(typeDesc.Elements) + "]";
		size += (((size - 1) / 16) + 1) * 16 * (typeDesc.Elements - 1);
	}

	target << ";";

	return 0;
}

int generateStruct(ID3D12ShaderReflectionType* type, UINT& size, std::ostream& of, std::map<std::string, UINT>& generatedStructs) {
	D3D12_SHADER_TYPE_DESC desc;
	HRESULT hr = type->GetDesc(&desc);
	CHECK_HR(Type / GetDesc);

	auto pib = generatedStructs.try_emplace(desc.Name);
	if (pib.second) {

		std::stringstream ss;

		ss << "struct " << desc.Name << "{\n";

		size = 0;

		for (UINT i = 0; i < desc.Members; ++i) {
			ID3D12ShaderReflectionType* memberType = type->GetMemberTypeByIndex(i);

			D3D12_SHADER_TYPE_DESC memberDesc;
			hr = memberType->GetDesc(&memberDesc);
			CHECK_HR(Type / Member / GetDesc);

			dummy(ss, (memberDesc.Offset - size) / 4);

			UINT memberSize = 0;

			if (int result = writeType(memberType, type->GetMemberTypeName(i), memberSize, ss, of, generatedStructs))
				return result;

			ss << " // offset: " << memberDesc.Offset << ", size: " << memberSize << "\n";

			size = memberDesc.Offset + memberSize;
		}

		ss << "}; // size: " << size;

		of << ss.str() << "\n\n";

		pib.first->second = size;
	}

	size = pib.first->second;

	return 0;
}

int generateConstantBufferStruct(ID3D12ShaderReflectionConstantBuffer* cb, std::ostream& of, std::map<std::string, UINT>& generatedStructs) {
	D3D12_SHADER_BUFFER_DESC desc;
	HRESULT hr = cb->GetDesc(&desc);
	CHECK_HR(ConstantBuffer / GetDesc);

	auto pib = generatedStructs.try_emplace(desc.Name);
	if (pib.second) {

		std::stringstream ss;

		ss << "struct " << desc.Name << "{\n";

		UINT currentSize = 0;

		for (UINT i = 0; i < desc.Variables; ++i) {
			ID3D12ShaderReflectionVariable* variable = cb->GetVariableByIndex(i);
			D3D12_SHADER_VARIABLE_DESC varDesc;
			hr = variable->GetDesc(&varDesc);
			CHECK_HR(ConstantBuffer / Variable / GetDesc);

			dummy(ss, (varDesc.StartOffset - currentSize) / 4);

			UINT size = 0;

			if (int result = writeType(variable->GetType(), varDesc.Name, size, ss, of, generatedStructs))
				return result;

			ss << "// offset: " << varDesc.StartOffset << ", size: " << size << "\n";

			if (size != varDesc.Size) {
				std::cerr << "Size mismatch: " << desc.Name << "::" << varDesc.Name << " " << size << " - " << varDesc.Size << std::endl;
				//return -1;
			}

			currentSize = varDesc.StartOffset + varDesc.Size;
		}

		ss << "}; // size: " << currentSize;

		of << ss.str() << "\n\n";

		pib.first->second = currentSize;
	}

	return 0;
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

		of << "inline const Engine::Render::ShaderMetadata file_" << baseName << "{\n\
    \"" << filePath << "\",\n\
    {\n";
		bool first = true;
		for (const std::wstring& include : includes) {
			if (first)
				first = false;
			else
				of << ", \n";
			of << "        \"" << converter.to_bytes(include) << "\"";
		}
		of << "\n    }\n\
};\n\n";

		std::map<std::string, UINT> generatedStructs;

		for (UINT i = 0; i < desc.FunctionCount; ++i) {
			ID3D12FunctionReflection* function = reflection->GetFunctionByIndex(i);
			D3D12_FUNCTION_DESC functionDesc;
			hr = function->GetDesc(&functionDesc);
			CHECK_HR(Reflect / Function / GetDesc);

			const char* s = functionDesc.Name;
			std::optional<FunctionSignature> signature = demangle(s);

			std::vector<std::string> constantBufferBindings;

			for (UINT j = 0; j < functionDesc.ConstantBuffers; ++j) {
				ID3D12ShaderReflectionConstantBuffer* cb = function->GetConstantBufferByIndex(j);
				if (int result = generateConstantBufferStruct(cb, of, generatedStructs))
					return result;

				D3D12_SHADER_BUFFER_DESC desc;
				hr = cb->GetDesc(&desc);
				CHECK_HR(Reflect / ConstantBuffer / GetDesc);

				D3D12_SHADER_INPUT_BIND_DESC bindDesc;
				hr = function->GetResourceBindingDescByName(desc.Name, &bindDesc);
				CHECK_HR(Reflect / ConstantBuffer / GetBinding);

				if (bindDesc.Space == 0) {
					if (constantBufferBindings.size() <= bindDesc.BindPoint)
						constantBufferBindings.resize(bindDesc.BindPoint + 1);
					if (!constantBufferBindings[bindDesc.BindPoint].empty() && constantBufferBindings[bindDesc.BindPoint] != desc.Name) {
						std::cerr << "Bind Point " << bindDesc.BindPoint << " used for " << desc.Name << " and " << constantBufferBindings[bindDesc.BindPoint] << std::endl;
						return -1;
					}
					constantBufferBindings[bindDesc.BindPoint] = desc.Name;
				}
			}

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

			of << "inline Engine::Render::ShaderFileObject<" << patchType(signature->returnType);

			for (std::string param : signature->parameterTypes) {
				of << ", " << patchType(param);
			}
			for (std::string binding : constantBufferBindings) {
				if (binding.empty()) {
					of << ", void";
				}
				else {
					of << ", " << binding;
				}
			}
			of << "> " << signature->name << " { file_" << baseName << ", \"" << signature->name << "\", {} };\n";
		}

		of << "}\n";

		std::cout << "Generated " << baseName << "_hlsl.h" << std::endl;;
	}
	else {
		std::cerr << "Unable to get reflection data from shader." << std::endl;
		return -1;
	}

	std::cout << "Success!" << std::endl;

	return 0;
}