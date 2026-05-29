#pragma once

#include <string>
#include <string_view>

namespace codeharness::execpolicy {

enum class ApprovalMode {
    UnlessTrusted,
    OnFailure,
    OnRequest,
    Reject,
    Never
};

enum class ApprovalRequirement {
    Skip,
    NeedsApproval,
    Forbidden
};

struct PolicyDecision {
    bool allowed{false};
    bool requires_approval{false};
    ApprovalRequirement requirement{ApprovalRequirement::Skip};
};

class ExecPolicyEngine {
public:
    auto evaluate(std::string_view command) -> PolicyDecision;
    void set_approval_mode(ApprovalMode mode);

private:
    ApprovalMode mode_{ApprovalMode::UnlessTrusted};
};

} // namespace codeharness::execpolicy
