#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <stdexcept>
#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

// Метод run вызывается для каждого совпадения с матчем. 
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto& Diag = Result.Context->getDiagnostics();
    auto& SM = *Result.SourceManager; // Получаем SourceManager для проверки isInMainFile
    
    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("classDecl")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("methodDecl");
        Method && Method->size_overridden_methods() > 0 && !Method->hasAttr<OverrideAttr>()) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("VarDecl")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

//todo: необходимо реализовать обработку случая невиртуального деструктора
void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor,
                            DiagnosticsEngine &Diag,
                            SourceManager &SM) {
    //Реализуйте Ваш код ниже
    if (virtualDtorLocations.contains(Dtor->getODRHash())) {
        return;
    }
    virtualDtorLocations.insert(Dtor->getODRHash());

    clang::SourceLocation Loc = Dtor->getLocation();
    if (!SM.isInMainFile(Loc)) {
        return;
    }

    const unsigned DiagID = Diag.getCustomDiagID(
            DiagnosticsEngine::Remark,
            "Объявлен деструктор"
        );
    Rewrite.InsertText(Loc, "virtual ");
    Diag.Report(Dtor->getLocation(), DiagID);
}

//todo: необходимо реализовать обработку случая отсутствие override
void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method,
                            DiagnosticsEngine &Diag,
                            SourceManager &SM) {
    //Реализуйте Ваш код ниже
    clang::SourceLocation Loc = Method->getLocation();
    if (!SM.isInMainFile(Loc)) {
        return;
    }

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлен метод");
    std::optional<Token> token;
    do {
        token = clang::Lexer::findNextToken(Loc, SM, Method->getASTContext().getLangOpts());
        Loc = token->getLocation();
        if (!token.has_value()) {
            return;
        }
    } while (!token->isOneOf(tok::l_brace, tok::semi));

    Rewrite.InsertTextBefore(Loc, " override ");
    Diag.Report(Method->getLocation(), DiagID);
}

//todo: необходимо реализовать обработку случая отсутствие & в range-for
void RefactorHandler::handle_crange_for(const VarDecl *LoopVar,
                                        DiagnosticsEngine &Diag,
                                        SourceManager &SM){
    // Реализуйте Ваш код ниже
    clang::SourceLocation Loc = LoopVar->getBeginLoc();
    if (!SM.isInMainFile(Loc)) {
        return;
    }

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлена переменная");
    std::optional<Token> Token;
    do {
        Token = clang::Lexer::findNextToken(Loc, SM, LoopVar->getASTContext().getLangOpts());
        Loc = Token->getLocation();
        if (!Token.has_value()) {
            return;
        }
    } while (!Token->is(tok::colon));
    Token = clang::Lexer::findPreviousToken(Loc, SM, LoopVar->getASTContext().getLangOpts(), false);

    Rewrite.InsertTextBefore(Token->getLocation(), "&");
    Diag.Report(LoopVar->getLocation(), DiagID);
}

//todo: ниже необходимо реализовать матчеры для поиска узлов AST
//note: синтаксис написания матчеров точно такой же как и для использования clang-query
/*
    Пример того, как может выглядеть реализация:
    auto AllClassesMatcher()
    {
        return cxxRecordDecl().bind("classDecl");
    }
*/
auto NvDtorMatcher()
{
    //todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска невиртуальных деструкторов
    return cxxRecordDecl(isDerivedFrom(cxxRecordDecl(
        hasMethod(cxxDestructorDecl(allOf(unless(isImplicit()), unless(isVirtual()))).bind("classDecl")))));
}

auto NoOverrideMatcher()
{
    //todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска методов без override
    return cxxMethodDecl(allOf(hasParent(cxxRecordDecl()), unless(hasAttr(attr::Override)), isOverride(),
                               unless(isImplicit()), unless(cxxDestructorDecl())))
        .bind("methodDecl");
}

auto NoRefConstVarInRangeLoopMatcher()
{
    //todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска range-for без &
    return cxxForRangeStmt(
        hasLoopVariable(varDecl(allOf(hasType(isConstQualified()), unless(hasType(builtinType())))).bind("VarDecl")));
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) {
    Finder.matchAST(Context);
}


std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI,
                                                StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(
        RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction( CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}
