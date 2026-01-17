#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <clang/AST/AST.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Type.h>

#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

auto NvDtorMatcher() {
    // clang-format off
    return cxxDestructorDecl(
        unless(isImplicit()),
        unless(isVirtual()),
        ofClass(cxxRecordDecl(isDefinition()).bind("base")),
        anyOf(
            ofClass(hasMethod(isVirtual())),
            hasAncestor(
                translationUnitDecl(
                    hasDescendant(
                        cxxRecordDecl(
                            isDefinition(),
                            isDerivedFrom(equalsBoundNode("base"))
                        )
                    )
                )
            )
        )
    ).bind("nonVirtualDtor");
    // clang-format on
}

auto NoOverrideMatcher() {
    // clang-format off
    return cxxMethodDecl(isOverride(),
                         unless(hasAttr(attr::Override)),
                         unless(isImplicit()),
                         unless(cxxDestructorDecl()))
        .bind("missingOverride");
    // clang-format on
}

auto NoRefConstVarInRangeLoopMatcher() {
    // clang-format off
    return cxxForRangeStmt(
        hasLoopVariable(
            varDecl(
                hasType(isConstQualified()),
                unless(hasType(referenceType())),
                unless(hasType(isAnyCharacter())),
                unless(hasType(builtinType()))
            ).bind("loopVar")
        )
    );
    // clang-format on
}

void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride");
        Method && Method->size_overridden_methods() > 0 && !Method->hasAttr<OverrideAttr>()) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Dtor->getLocation()))
        return;

    unsigned LocHash = SM.getFileOffset(Dtor->getLocation());
    if (virtualDtorLocations.count(LocHash))
        return;
    virtualDtorLocations.insert(LocHash);

    const unsigned DiagID =
        Diag.getCustomDiagID(DiagnosticsEngine::Warning, "Adding 'virtual' to base class destructor");
    Diag.Report(Dtor->getLocation(), DiagID);

    Rewrite.InsertText(Dtor->getInnerLocStart(), "virtual ", true, true);
}
void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Method->getLocation()))
        return;

    SourceLocation RParenLoc;
    if (auto *TSI = Method->getTypeSourceInfo()) {
        if (auto FTL = TSI->getTypeLoc().getAs<FunctionTypeLoc>()) {
            RParenLoc = FTL.getRParenLoc();
        }
    }

    if (RParenLoc.isInvalid()) {
        RParenLoc = Lexer::findLocationAfterToken(Method->getLocation(), tok::r_paren, SM,
                                                  Method->getASTContext().getLangOpts(), false);
    }

    if (RParenLoc.isValid()) {
        const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Warning,
                                                     "Method overrides base but lacks 'override'; adding specifier");
        Diag.Report(Method->getLocation(), DiagID);

        SourceLocation AfterRParen =
            Lexer::getLocForEndOfToken(RParenLoc, 0, SM, Method->getASTContext().getLangOpts());
        Rewrite.InsertText(AfterRParen, " override", true, true);
    }
}

void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(LoopVar->getLocation()))
        return;

    const unsigned DiagID = Diag.getCustomDiagID(
        DiagnosticsEngine::Warning, "Range-for variable is const but not a reference; adding '&' to avoid copy");
    Diag.Report(LoopVar->getLocation(), DiagID);

    SourceLocation IdentifierLoc = LoopVar->getLocation();

    Rewrite.InsertTextAfter(IdentifierLoc, "&");
}

ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;
}

void CodeRefactorAction::EndSourceFileAction() {
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}