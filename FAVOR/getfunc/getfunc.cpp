#include <iostream>
#include <string>
#include <vector>
#include <fstream>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/Lexer.h"

using namespace clang;
using namespace clang::tooling;

class ExtractFunctionVisitor : public RecursiveASTVisitor<ExtractFunctionVisitor> {
public:
    explicit ExtractFunctionVisitor(ASTContext *Context, int targetLine, std::string &filePath)
        : Context(Context), targetLine(targetLine), out(filePath),filepath(filePath) {} 

    bool isFromMainFile(SourceLocation loc) {
        return Context->getSourceManager().isInMainFile(loc);
    }

    std::string getSourceCode(SourceRange range) {
        SourceManager &SM = Context->getSourceManager();
        SourceLocation startLoc = range.getBegin();
        SourceLocation endLoc = Lexer::getLocForEndOfToken(range.getEnd(), 0, SM, LangOptions());
        unsigned length = SM.getFileOffset(endLoc) - SM.getFileOffset(startLoc);
        return std::string(SM.getCharacterData(startLoc), length);
    }
    

    bool VisitFunctionDecl(FunctionDecl *FD) {
        if (FD->hasBody() && isFromMainFile(FD->getLocation())) {
            Stmt *body = FD->getBody();
            SourceRange range = body->getSourceRange();
            FullSourceLoc startLoc = Context->getFullLoc(range.getBegin());
            FullSourceLoc endLoc = Context->getFullLoc(range.getEnd());
    
            if (startLoc.isValid() && endLoc.isValid() &&
                startLoc.getSpellingLineNumber() <= targetLine &&
                endLoc.getSpellingLineNumber() >= targetLine) {
    
                std::string functionSourceCode = getSourceCode(FD->getSourceRange());
                out << functionSourceCode;  
                functionFound = true;
            }
        }
        return true;
    }

    ~ExtractFunctionVisitor() {
        if (!functionFound) {
            std::cerr << "Function: " << filepath << " " << targetLine << " error\n";
        }
    }
private:
    ASTContext *Context;
    int targetLine;
    std::ofstream out;
    bool functionFound = false;
    std::string filepath;
};


class ExtractFunctionConsumer : public clang::ASTConsumer {
public:
    explicit ExtractFunctionConsumer(ASTContext *Context, int targetLine, std::string &filePath)
        : Visitor(Context, targetLine, filePath) {}
    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    ExtractFunctionVisitor Visitor;
};


class ExtractFunctionAction : public clang::ASTFrontendAction {
public:
    ExtractFunctionAction(int targetLine, std::string &filePath) 
        : targetLine(targetLine), filePath(filePath) {}

    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new ExtractFunctionConsumer(&Compiler.getASTContext(), targetLine, filePath));
    }

private:
    int targetLine;
    std::string filePath;
};

class ExtractFunctionActionFactory : public FrontendActionFactory {
public:
    ExtractFunctionActionFactory(int targetLine, std::string &filePath)
        : targetLine(targetLine), filePath(filePath) {}

    std::unique_ptr<FrontendAction> create() override {
        return std::make_unique<ExtractFunctionAction>(targetLine, filePath);
    }

private:
    int targetLine;
    std::string filePath;
};
class IgnoringDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
    void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel, const Diagnostic &Info) override {
        // Ignore all diagnostics.
    }
};

int main(int argc, const char **argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <source_file> <target_line> <output_file>\n";
        return 1;
    }
    std::string filePath = argv[1];
    int targetLine = std::stoi(argv[2]);
    std::string outFilePath = argv[3];

    std::vector<std::string> sourcePathList = {filePath};

    FixedCompilationDatabase compilations(".", std::vector<std::string>());
    ClangTool Tool(compilations, sourcePathList);
    ExtractFunctionActionFactory factory(targetLine, outFilePath);
    Tool.setDiagnosticConsumer(new IgnoringDiagnosticConsumer());
    return Tool.run(&factory);
}