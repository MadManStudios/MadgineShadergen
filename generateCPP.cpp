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
	/*size_t i = s.find("vector<float, ");
	while (i != std::string::npos) {
		s.replace(i, i + 14, "Engine::Vector");
		s.erase(i + 15);
		i = s.find("vector<float, ");
	}*/

	return "\"" + s + "\"";
}

int generateStruct(ID3D12ShaderReflectionType* type, UINT& size, std::ostream& of, std::ostream& of_cpp, bool generateMeta, std::map<std::string, UINT>& generatedStructs);

int writeType(ID3D12ShaderReflectionType* type, const char* name, UINT& size, std::ostream& target, std::ostream& of, std::ostream& of_cpp, bool generateMeta, std::map<std::string, UINT>& generatedStructs) {

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
		if (int result = generateStruct(type, size, of, of_cpp, generateMeta, generatedStructs))
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

int generateStruct(ID3D12ShaderReflectionType* type, UINT& size, std::ostream& of, std::ostream &of_cpp, bool generateMeta, std::map<std::string, UINT>& generatedStructs) {
	D3D12_SHADER_TYPE_DESC desc;
	HRESULT hr = type->GetDesc(&desc);
	CHECK_HR(Type / GetDesc);

	auto pib = generatedStructs.try_emplace(desc.Name);
	if (pib.second) {

		std::stringstream ss;
		std::stringstream ss_keyvalue;
		std::stringstream ss_serialize;

		ss << "struct " << desc.Name << "{\n";

		ss_keyvalue << "METATABLE_BEGIN(HLSL::" << desc.Name << ");\n";
		ss_serialize << "SERIALIZETABLE_BEGIN(HLSL::" << desc.Name << ");\n";

		size = 0;

		for (UINT i = 0; i < desc.Members; ++i) {
			ID3D12ShaderReflectionType* memberType = type->GetMemberTypeByIndex(i);

			D3D12_SHADER_TYPE_DESC memberDesc;
			hr = memberType->GetDesc(&memberDesc);
			CHECK_HR(Type / Member / GetDesc);

			dummy(ss, (memberDesc.Offset - size) / 4);

			UINT memberSize = 0;

			if (int result = writeType(memberType, type->GetMemberTypeName(i), memberSize, ss, of, of_cpp, generateMeta, generatedStructs))
				return result;

			ss << " // offset: " << memberDesc.Offset << ", size: " << memberSize << "\n";

			ss_keyvalue << "    MEMBER(" << type->GetMemberTypeName(i) << ");\n";
			ss_serialize << "    FIELD(" << type->GetMemberTypeName(i) << ");\n";

			size = memberDesc.Offset + memberSize;
		}

		ss << "}; // size: " << size;

		ss_keyvalue << "METATABLE_END(HLSL::" << desc.Name << ");\n\n";
		ss_serialize << "SERIALIZETABLE_END(HLSL::" << desc.Name << ");\n\n";

		of << ss.str() << "\n\n";
		if (generateMeta) {
			of_cpp << ss_keyvalue.str() << ss_serialize.str() << "\n";
		}

		pib.first->second = size;
	}

	size = pib.first->second;

	return 0;
}

int generateConstantBufferStruct(ID3D12ShaderReflectionConstantBuffer* cb, std::ostream& of, std::ostream& of_cpp, bool generateMeta, std::map<std::string, UINT>& generatedStructs) {
	D3D12_SHADER_BUFFER_DESC bufferDesc;
	HRESULT hr = cb->GetDesc(&bufferDesc);
	CHECK_HR(Reflect / ConstantBuffer / GetBufferDesc);

	assert(bufferDesc.Variables == 1);

	ID3D12ShaderReflectionType* type = cb->GetVariableByIndex(0)->GetType();

	D3D12_SHADER_TYPE_DESC typeDesc;
	hr = type->GetDesc(&typeDesc);
	CHECK_HR(Reflect / ConstantBuffer / GetTypeDesc);

	UINT size;
	return generateStruct(type, size, of, of_cpp, generateMeta, generatedStructs);
}

int generateCPP(const std::wstring& _filePath, const std::wstring& outFolder, const DxcBuffer& sourceBuffer, std::vector<LPCWSTR> arguments, std::vector<std::wstring> includes, std::map<std::wstring, std::vector<std::wstring>>& profileEntrypoints) {

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
		std::ofstream of_cpp{ converter.to_bytes(outFolder) + '/' + baseName + "_hlsl.cpp" };

		of_cpp << "#include \"" << baseName << "_hlsl.h\"\n\n";

		of_cpp << R"(#include "Meta/keyvalue/metatable_impl.h"
#include "Meta/serialize/serializetable_impl.h"

)";

		of << "#pragma once\n";

		of << R"(
	#include "Madgine/renderlib.h"
    #include "Madgine/render/shaderfileobject.h"
	#include "Madgine/render/textureloader.h"
	#include "Meta/math/matrix4.h"

)";

		of << "namespace HLSL {\n";

		of << "inline const Engine::Render::ShaderMetadata file_" << baseName << "{\n\
    \"" << filePath << "\",\n\
    {\n";

		std::sort(includes.begin(), includes.end());

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

			struct ResourceMember {
				std::string mName;
				std::string mType;
			};

			struct ResourceBlock {
				std::string mName;
				bool mIsSingleTexture;
				std::vector<ResourceMember> mMembers;
			};

			std::vector<ResourceBlock> resourceBlocks;

			for (UINT j = 0; j < functionDesc.ConstantBuffers; ++j) {
				ID3D12ShaderReflectionConstantBuffer* cb = function->GetConstantBufferByIndex(j);

				D3D12_SHADER_BUFFER_DESC desc;
				hr = cb->GetDesc(&desc);
				CHECK_HR(Reflect / ConstantBuffer / GetDesc);

				D3D12_SHADER_INPUT_BIND_DESC bindDesc;
				hr = function->GetResourceBindingDescByName(desc.Name, &bindDesc);
				CHECK_HR(Reflect / ConstantBuffer / GetBinding);

				if (int result = generateConstantBufferStruct(cb, of, of_cpp, bindDesc.Space > 1, generatedStructs))
					return result;

				if (bindDesc.Space == 0) {
					if (constantBufferBindings.size() <= bindDesc.BindPoint)
						constantBufferBindings.resize(bindDesc.BindPoint + 1);
					if (!constantBufferBindings[bindDesc.BindPoint].empty() && constantBufferBindings[bindDesc.BindPoint] != desc.Name) {
						std::cerr << "Bind Point " << bindDesc.BindPoint << " used for " << desc.Name << " and " << constantBufferBindings[bindDesc.BindPoint] << std::endl;
						return -1;
					}

					D3D12_SHADER_TYPE_DESC typeDesc;
					hr = cb->GetVariableByIndex(0)->GetType()->GetDesc(&typeDesc);
					CHECK_HR(Reflect / ConstantBuffer / GetTypeDesc);

					constantBufferBindings[bindDesc.BindPoint] = typeDesc.Name;
				}
			}

			for (UINT j = 0; j < functionDesc.BoundResources; ++j) {
				D3D12_SHADER_INPUT_BIND_DESC bindDesc;
				hr = function->GetResourceBindingDesc(j, &bindDesc);
				CHECK_HR(Reflect / ResourceBinding / GetDesc);

				if (bindDesc.Space < 2)
					continue;

				std::string type;

				switch (bindDesc.Type) {
				case D3D_SIT_TEXTURE:
					type = "Engine::Render::TextureLoader::Handle";
					break;
				case D3D_SIT_STRUCTURED:
				case D3D_SIT_CBUFFER: {
					ID3D12ShaderReflectionConstantBuffer* buffer = function->GetConstantBufferByName(bindDesc.Name);
					D3D12_SHADER_BUFFER_DESC bufferDesc;
					hr = buffer->GetDesc(&bufferDesc);
					CHECK_HR(Reflect / ResourceBinding / GetBufferDesc);

					assert(bufferDesc.Variables == 1);
					D3D12_SHADER_TYPE_DESC typeDesc;
					buffer->GetVariableByIndex(0)->GetType()->GetDesc(&typeDesc);					
					
					type = typeDesc.Name;

					break;
				}
				default:
					std::cerr << "Unsupported resource type: " << bindDesc.Type << std::endl;
				case D3D_SIT_SAMPLER:
					continue;
				}

				if (resourceBlocks.size() <= bindDesc.Space) {
					resourceBlocks.resize(bindDesc.Space + 1);
					resourceBlocks[bindDesc.Space].mName = signature->name + "ResourceBlock" + std::to_string(bindDesc.Space);
				}
				ResourceBlock& block = resourceBlocks[bindDesc.Space];

				block.mMembers.push_back(ResourceMember{ bindDesc.Name, type });
				block.mIsSingleTexture = bindDesc.Type == D3D_SIT_TEXTURE && block.mMembers.size() == 1;
			}

			for (const ResourceBlock& block : resourceBlocks) {
				if (block.mMembers.empty())
					continue;
				of << "struct " << block.mName << " {\n";
				of_cpp << "METATABLE_BEGIN(HLSL::" << block.mName << ")\n";
				for (const ResourceMember& member : block.mMembers) {
					of << "    " << member.mType << " " << member.mName << ";\n";
					of_cpp << "    MEMBER(" << member.mName << ")\n";
				}
				of_cpp << "METATABLE_END(HLSL::" << block.mName << ")\n\n";
				if (block.mIsSingleTexture) {
					of << "\n    Engine::Render::ResourceBlock";
				}
				else {
					of << "\n    Engine::Render::UniqueResourceBlock";
				}
				of << " toResourceBlock(Engine::Render::RenderContext * context) const; \n }; \n\n";

				of_cpp << "SERIALIZETABLE_BEGIN(HLSL::" << block.mName << ")\n";
				for (const ResourceMember& member : block.mMembers) {
					of_cpp << "    FIELD(" << member.mName << ")\n";
				}
				of_cpp << "SERIALIZETABLE_END(HLSL::" << block.mName << ")\n\n\n";
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