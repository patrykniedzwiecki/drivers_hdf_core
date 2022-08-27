/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */

#include "codegen/c_interface_code_emitter.h"
#include "util/file.h"
#include "util/logger.h"

namespace OHOS {
namespace HDI {
bool CInterfaceCodeEmitter::ResolveDirectory(const std::string &targetDirectory)
{
    if (ast_->GetASTFileType() == ASTFileType::AST_IFACE || ast_->GetASTFileType() == ASTFileType::AST_ICALLBACK) {
        directory_ = GetFileParentPath(targetDirectory);
    } else {
        return false;
    }

    if (!File::CreateParentDir(directory_)) {
        Logger::E("CInterfaceCodeEmitter", "Create '%s' failed!", directory_.c_str());
        return false;
    }

    return true;
}

void CInterfaceCodeEmitter::EmitCode()
{
    EmitInterfaceHeaderFile();
}

void CInterfaceCodeEmitter::EmitInterfaceHeaderFile()
{
    std::string filePath =
        File::AdapterPath(StringHelper::Format("%s/%s.h", directory_.c_str(), FileName(interfaceName_).c_str()));
    File file(filePath, File::WRITE);
    StringBuilder sb;

    EmitLicense(sb);
    EmitHeadMacro(sb, interfaceFullName_);
    sb.Append("\n");
    EmitImportInclusions(sb);
    sb.Append("\n");
    EmitHeadExternC(sb);
    sb.Append("\n");
    EmitPreDeclaration(sb);
    sb.Append("\n");
    EmitInterfaceDesc(sb);
    sb.Append("\n");
    EmitInterfaceVersionMacro(sb);
    sb.Append("\n");
    EmitInterfaceBuffSizeMacro(sb);
    sb.Append("\n");
    EmitInterfaceMethodCommands(sb, "");
    sb.Append("\n");
    EmitInterfaceDefinition(sb);
    sb.Append("\n");
    EmitInterfaceGetMethodDecl(sb);
    sb.Append("\n");
    EmitInterfaceReleaseMethodDecl(sb);
    sb.Append("\n");
    EmitTailExternC(sb);
    sb.Append("\n");
    EmitTailMacro(sb, interfaceFullName_);

    std::string data = sb.ToString();
    file.WriteData(data.c_str(), data.size());
    file.Flush();
    file.Close();
}

void CInterfaceCodeEmitter::EmitImportInclusions(StringBuilder &sb)
{
    HeaderFile::HeaderFileSet headerFiles;

    GetImportInclusions(headerFiles);
    GetHeaderOtherLibInclusions(headerFiles);

    for (const auto &file : headerFiles) {
        sb.AppendFormat("%s\n", file.ToString().c_str());
    }
}

void CInterfaceCodeEmitter::GetHeaderOtherLibInclusions(HeaderFile::HeaderFileSet &headerFiles)
{
    if (!Options::GetInstance().DoGenerateKernelCode()) {
        headerFiles.emplace(HeaderFileType::C_STD_HEADER_FILE, "stdint");
        headerFiles.emplace(HeaderFileType::C_STD_HEADER_FILE, "stdbool");
    }
}

void CInterfaceCodeEmitter::EmitPreDeclaration(StringBuilder &sb)
{
    sb.Append("struct HdfRemoteService;\n");
}

void CInterfaceCodeEmitter::EmitInterfaceDesc(StringBuilder &sb)
{
    sb.AppendFormat("#define %s \"%s\"\n", interface_->EmitDescMacroName().c_str(), interfaceFullName_.c_str());
}

void CInterfaceCodeEmitter::EmitInterfaceVersionMacro(StringBuilder &sb)
{
    sb.AppendFormat("#define %s %u\n", majorVerName_.c_str(), ast_->GetMajorVer());
    sb.AppendFormat("#define %s %u\n", minorVerName_.c_str(), ast_->GetMinorVer());
}

void CInterfaceCodeEmitter::EmitInterfaceBuffSizeMacro(StringBuilder &sb)
{
    sb.AppendFormat("#define %s    (4 * 1024) // 4KB\n", bufferSizeMacroName_.c_str());
}

void CInterfaceCodeEmitter::EmitInterfaceDefinition(StringBuilder &sb)
{
    sb.AppendFormat("struct %s {\n", interfaceName_.c_str());
    EmitInterfaceMethods(sb, TAB);
    sb.Append("};\n");
}

void CInterfaceCodeEmitter::EmitInterfaceMethods(StringBuilder &sb, const std::string &prefix)
{
    for (size_t i = 0; i < interface_->GetMethodNumber(); i++) {
        AutoPtr<ASTMethod> method = interface_->GetMethod(i);
        EmitInterfaceMethod(method, sb, prefix);
        sb.Append("\n");
    }

    EmitInterfaceMethod(interface_->GetVersionMethod(), sb, prefix);

    if (!isKernelCode_) {
        sb.Append("\n");
        EmitAsObjectMethod(sb, TAB);
    }
}

void CInterfaceCodeEmitter::EmitInterfaceMethod(
    const AutoPtr<ASTMethod> &method, StringBuilder &sb, const std::string &prefix)
{
    if (method->GetParameterNumber() == 0) {
        sb.Append(prefix).AppendFormat(
            "int32_t (*%s)(struct %s *self);\n", method->GetName().c_str(), interfaceName_.c_str());
    } else {
        StringBuilder paramStr;
        paramStr.Append(prefix).AppendFormat(
            "int32_t (*%s)(struct %s *self, ", method->GetName().c_str(), interfaceName_.c_str());
        for (size_t i = 0; i < method->GetParameterNumber(); i++) {
            AutoPtr<ASTParameter> param = method->GetParameter(i);
            EmitInterfaceMethodParameter(param, paramStr, "");
            if (i + 1 < method->GetParameterNumber()) {
                paramStr.Append(", ");
            }
        }

        paramStr.Append(");");
        sb.Append(SpecificationParam(paramStr, prefix + TAB));
        sb.Append("\n");
    }
}

void CInterfaceCodeEmitter::EmitAsObjectMethod(StringBuilder &sb, const std::string &prefix)
{
    sb.Append(prefix).AppendFormat("struct HdfRemoteService* (*AsObject)(struct %s *self);\n", interfaceName_.c_str());
}

void CInterfaceCodeEmitter::EmitInterfaceGetMethodDecl(StringBuilder &sb)
{
    if (isKernelCode_) {
        sb.AppendFormat("struct %s *%sGet(void);\n", interfaceName_.c_str(), interfaceName_.c_str());
        sb.Append("\n");
        sb.AppendFormat(
            "struct %s *%sGetInstance(const char *instanceName);\n", interfaceName_.c_str(), interfaceName_.c_str());
        return;
    }

    if (interface_->IsSerializable()) {
        sb.Append("// no external method used to create client object, it only support ipc mode\n");
        sb.AppendFormat("struct %s *%sGet(struct HdfRemoteService *remote);\n", interfaceName_.c_str(),
            interfaceName_.c_str());
    } else {
        sb.Append("// external method used to create client object, it support ipc and passthrought mode\n");
        sb.AppendFormat("struct %s *%sGet(bool isStub);\n", interfaceName_.c_str(), interfaceName_.c_str());
        sb.AppendFormat("struct %s *%sGetInstance(const char *serviceName, bool isStub);\n", interfaceName_.c_str(),
            interfaceName_.c_str());
    }
}

void CInterfaceCodeEmitter::EmitInterfaceReleaseMethodDecl(StringBuilder &sb)
{
    if (isKernelCode_) {
        sb.AppendFormat("void %sRelease(struct %s *instance);\n", interfaceName_.c_str(), interfaceName_.c_str());
        return;
    }

    if (interface_->IsCallback()) {
        sb.Append("// external method used to release client object, it support ipc and passthrought mode\n");
        sb.AppendFormat("void %sRelease(struct %s *instance);\n", interfaceName_.c_str(),
            interfaceName_.c_str());
    } else if (interface_->IsSerializable()) {
        sb.Append("// external method used to release client object, it support ipc and passthrought mode\n");
        sb.AppendFormat("void %sRelease(struct %s *instance, bool isStub);\n", interfaceName_.c_str(),
            interfaceName_.c_str());
    } else {
        sb.Append("// external method used to create release object, it support ipc and passthrought mode\n");
        sb.AppendFormat("void %sRelease(struct %s *instance, bool isStub);\n", interfaceName_.c_str(),
            interfaceName_.c_str());
        sb.AppendFormat("void %sReleaseInstance(const char *serviceName, struct %s *instance, bool isStub);\n",
            interfaceName_.c_str(), interfaceName_.c_str());
    }
}
} // namespace HDI
} // namespace OHOS