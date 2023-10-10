#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <cstdlib>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace clang::tooling;

#include "clang/Analysis/CFG.h"
#include "llvm/ADT/Optional.h"
using llvm::Optional;

class ExtractCodePathsVisitor : public RecursiveASTVisitor<ExtractCodePathsVisitor> {
public:
    explicit ExtractCodePathsVisitor(ASTContext *Context, int bugLocation)
        : Context(Context), visitedFunction(false), pathCount(0), bugLoc(bugLocation) {}

    bool isFromMainFile(SourceLocation loc) {
        return Context->getSourceManager().isInMainFile(loc);
    }

    bool VisitFunctionDecl(FunctionDecl *FD) {
        if (!visitedFunction && FD->hasBody() && isFromMainFile(FD->getLocation())) {
            currentFunction = FD;  // Remember the current function.
            FullSourceLoc startLoc(FD->getBeginLoc(), Context->getSourceManager());
            FullSourceLoc endLoc(FD->getEndLoc(), Context->getSourceManager());

            unsigned int startLine = startLoc.getSpellingLineNumber();
            unsigned int endLine = endLoc.getSpellingLineNumber();
            if(bugLoc >= startLine && bugLoc <= endLine){
              auto cfg = CFG::buildCFG(FD, FD->getBody(), Context, CFG::BuildOptions());
              if (cfg) {
                  std::vector<CFGBlock *> path;
                  std::unordered_set<CFGBlock *> visited;
                  DFS(cfg.get(), &cfg->getEntry(), path, visited);
                  visitedFunction = true;  // Mark that we have visited a function.
              }
            }
        }
        return true;
    }
    void incrementAndCheckPathCount() {
        if (++pathCount > 1500) {
            std::cout.flush();
            std::exit(0);
        }
    }

    void DFS(std::unique_ptr<clang::CFG>::pointer cfg, CFGBlock *block, std::vector<CFGBlock *> &path, std::unordered_set<CFGBlock *> &visited) {
        path.push_back(block);
        visited.insert(block);

        if (block == &cfg->getExit()) {
            incrementAndCheckPathCount();
            printPath(path);
        } else {
                for (auto succ = block->succ_begin(), succEnd = block->succ_end(); succ != succEnd; ++succ) {
                    CFGBlock *succBlock = *succ;
                    if (visited.find(succBlock) == visited.end()) {
                        DFS(cfg, succBlock, path, visited);
                    }else if(std::find(path.begin(), path.end(), succBlock) != path.end()){
                        auto CondBlockIt = std::find(path.begin(), path.end(), succBlock);
                        CFGBlock *CondBlock = *CondBlockIt;
                        for(auto succofCond = CondBlock->succ_begin(), succofCondEnd = CondBlock->succ_end(); succofCond != succofCondEnd; ++succofCond){
                            CFGBlock *SuccofCond = *succofCond;
                            if (std::find(path.begin(), path.end(), SuccofCond) == path.end()) {
                                DFS(cfg, SuccofCond, path, visited);
                            }
                        }
                    }
                }
        }

        path.pop_back();
        visited.erase(block);
    }
    std::string getSourceCode(SourceRange range, const SourceManager &SM) {
        SourceLocation startLoc = range.getBegin();
        SourceLocation endLoc = Lexer::getLocForEndOfToken(range.getEnd(), 0, SM, LangOptions());
        unsigned length = SM.getFileOffset(endLoc) - SM.getFileOffset(startLoc);
        return std::string(SM.getCharacterData(startLoc), length);
    }

    void printPath(const std::vector<CFGBlock *> &path) {
        static int pathCount = 0;
        std::cout << "Path " << ++pathCount << ":\n";
        std::cout << "Start\n";
        FullSourceLoc fullLoc(currentFunction->getBeginLoc(), Context->getSourceManager());
        unsigned int lineNumber = fullLoc.getSpellingLineNumber();
        std::string signature = getFunctionSignature(currentFunction);
        std::cout << " line " << lineNumber << " Function: " << signature << "\n";
        for (const auto *block : path) {
            if (block->empty())
                continue;

            for (const auto &elem : *block) {
                if (Optional<CFGStmt> CS = elem.getAs<CFGStmt>()) {
                    Stmt *S = const_cast<Stmt *>(CS->getStmt());
                    FullSourceLoc fullLoc(S->getBeginLoc(), Context->getSourceManager());
                    std::string sourceCode = getSourceCode(S->getSourceRange(), Context->getSourceManager());
                    // 获取并输出源代码行号
                    unsigned int lineNumber = fullLoc.getSpellingLineNumber();
                    std::cout << " line " << lineNumber << " " << sourceCode <<  "\n";
                }
            }
        }
        std::cout << "End\n\n";
    }

private:
    ASTContext *Context;
    bool visitedFunction;  // Added this variable to track if we have visited a function.
    int pathCount;  // Keep track of how many paths we have printed.
    int bugLoc;
    FunctionDecl *currentFunction;  // Remember the current function.
    std::string getFunctionSignature(FunctionDecl *FD) {
        std::string funcSignature;
        
        // 获取并添加返回类型
        funcSignature += FD->getReturnType().getAsString();
        funcSignature += " ";
        
        // 获取并添加函数名称
        funcSignature += FD->getNameInfo().getAsString();
        
        // 添加参数列表
        funcSignature += "(";
        for (auto it = FD->param_begin(); it != FD->param_end(); ++it) {
            if (it != FD->param_begin()) {
                funcSignature += ", ";
            }
            funcSignature += (*it)->getType().getAsString();
        }
        funcSignature += ")";
        
        return funcSignature;
    }
};

class ExtractCodePathsConsumer : public clang::ASTConsumer {
public:
    explicit ExtractCodePathsConsumer(ASTContext *Context, int bugLocation)
        : Visitor(Context, bugLocation) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    ExtractCodePathsVisitor Visitor;
};

class ExtractCodePathsAction : public clang::ASTFrontendAction {
public:
    ExtractCodePathsAction(int bugLocation):bugLoc(bugLocation) {}

    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
        return std::unique_ptr<clang::ASTConsumer>(
            new ExtractCodePathsConsumer(&Compiler.getASTContext(), bugLoc));
    }

private:
    int bugLoc;
};

class ExtractCodePathsActionFactory : public FrontendActionFactory {
public:
    ExtractCodePathsActionFactory(int bugLocation):bugLoc(bugLocation) {}

    std::unique_ptr<FrontendAction> create() override {
        return std::make_unique<ExtractCodePathsAction>(bugLoc);
    }
private:
 int bugLoc;
};


class IgnoringDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
    void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel, const Diagnostic &Info) override {
        // Ignore all diagnostics.
    }
};

int main(int argc, const char **argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <source_file> <bug_loc> <macros...>\n";
        return 1;
    }

    std::string filePath = argv[1];
    int bugLoc = std::stoi(argv[2]); 
    std::vector<std::string> macros(argv + 2, argv + argc);

    // 创建预处理指令参数
    std::vector<std::string> compileArgs;
    for (const auto& macro : macros) {
        compileArgs.push_back("-D" + macro);
    }

    std::vector<std::string> sourcePathList = {filePath};

    // 将预处理指令参数传递给FixedCompilationDatabase
    FixedCompilationDatabase compilations(".", compileArgs);
    ClangTool Tool(compilations, sourcePathList);
    ExtractCodePathsActionFactory factory(bugLoc);

    Tool.setDiagnosticConsumer(new IgnoringDiagnosticConsumer());

    return Tool.run(&factory);
}