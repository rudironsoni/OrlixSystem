#include "IXLandAbstractionLeakageCheck.h"
#include "IXLandAbiSurfaceCheck.h"
#include "IXLandHostVocabularyCheck.h"
#include "IXLandIncludeBoundaryCheck.h"
#include "IXLandLoggingPolicyCheck.h"
#include "IXLandSourcePolicyCheck.h"
#include "IXLandTestPolicyCheck.h"
#include "IXLandTestVocabularyCheck.h"
#include "IXLandTypeOwnershipCheck.h"

#include "clang-tidy/ClangTidyModule.h"

namespace clang::tidy::ixland {

class IXLandTidyModule : public ClangTidyModule {
public:
  void addCheckFactories(ClangTidyCheckFactories &Factories) override {
    Factories.registerCheck<IXLandAbstractionLeakageCheck>(
        "ixland-abstraction-leakage");
    Factories.registerCheck<IXLandAbiSurfaceCheck>("ixland-abi-surface");
    Factories.registerCheck<IXLandHostVocabularyCheck>(
        "ixland-host-vocabulary");
    Factories.registerCheck<IXLandIncludeBoundaryCheck>(
        "ixland-include-boundary");
    Factories.registerCheck<IXLandLoggingPolicyCheck>("ixland-logging-policy");
    Factories.registerCheck<IXLandSourcePolicyCheck>("ixland-source-policy");
    Factories.registerCheck<IXLandTestPolicyCheck>("ixland-test-policy");
    Factories.registerCheck<IXLandTestVocabularyCheck>(
        "ixland-test-vocabulary");
    Factories.registerCheck<IXLandTypeOwnershipCheck>(
        "ixland-type-ownership");
  }
};

static ClangTidyModuleRegistry::Add<IXLandTidyModule>
    X("ixland-module", "IXLand custom source policy checks.");

volatile int IXLandTidyModuleAnchorSource = 0;

} // namespace clang::tidy::ixland
