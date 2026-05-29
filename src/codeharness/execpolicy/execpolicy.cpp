#include "execpolicy.hpp"

namespace codeharness::execpolicy {

auto ExecPolicyEngine::evaluate(std::string_view command) -> PolicyDecision {
    return PolicyDecision{};
}

void ExecPolicyEngine::set_approval_mode(ApprovalMode mode) {
    mode_ = mode;
}

} // namespace codeharness::execpolicy
