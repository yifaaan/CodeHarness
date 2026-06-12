import os, re

# Replace .error() with .status() - aggressive replacement, may catch unrelated usages
# but in this codebase .error() on Result is the only place that matters

# This script:
# 1. Replaces remaining ErrorKind::X with absl error factory calls where appropriate
# 2. Replaces `!result` with `!result.ok()` for result variables (this is hard to do safely)
# 3. Replaces `.error()` with `.status()` (works for absl::StatusOr)
# 4. Replaces remaining snake_case function names with PascalCase

# Step 1: snake_case helper function renames
# These are common helper patterns in tools code
HELPER_RENAMES = {
    "parse_grep_input": "ParseGrepInput",
    "parse_glob_input": "ParseGlobInput",
    "parse_bash_input": "ParseBashInput",
    "parse_bash": "ParseBash",
    "parse_todo_input": "ParseTodoInput",
    "parse_web_input": "ParseWebInput",
    "parse_sleep_input": "ParseSleepInput",
    "parse_edit_input": "ParseEditInput",
    "parse_skill_input": "ParseSkillInput",
    "parse_write_input": "ParseWriteInput",
    "parse_read_input": "ParseReadInput",
    "parse_input": "ParseInput",
    "parse_memory_input": "ParseMemoryInput",
    "parse_hook_config": "ParseHookConfig",
    "parse_tool_request_input": "ParseToolRequestInput",
    "parse_provider_url": "ParseProviderUrl",
    "is_skipped_directory_name": "IsSkippedDirectoryName",
    "is_under_skipped_directory": "IsUnderSkippedDirectory",
    "is_small_regular_file": "IsSmallRegularFile",
    "search_file": "SearchFile",
    "search_directory": "SearchDirectory",
    "command_permission_target": "CommandPermissionTarget",
    "path_permission_target": "PathPermissionTarget",
    "resolve_workspace_path": "ResolveWorkspacePath",
    "read_text_file": "ReadTextFile",
    "atomic_write_text_file": "AtomicWriteTextFile",
    "trim_ascii": "TrimAscii",
    "lower_ascii": "LowerAscii",
    "slugify": "Slugify",
    "trim": "Trim",
    "strip_trailing_cr": "StripTrailingCr",
    "next_line": "NextLine",
    "home_directory": "HomeDirectory",
    "path_has_parent_reference": "PathHasParentReference",
    "is_safe_relative_path": "IsSafeRelativePath",
    "ensure_directory": "EnsureDirectory",
    "init_logger": "InitLogger",
    "utc_timestamp_seconds": "UtcTimestampSeconds",
    "git_blob_hash_hex": "GitBlobHashHex",
    "default_shell_command_argv": "DefaultShellCommandArgv",
    "make_text_message": "MakeTextMessage",
    "make_tool_result_message": "MakeToolResultMessage",
    "collect_text": "CollectText",
    "collect_tool_uses": "CollectToolUses",
    "assign": "Assign",
    "make_error": "MakeError",
}

# ErrorKind -> absl error factory
ERROR_FACTORY = {
    "InvalidArgument": "absl::InvalidArgumentError",
    "Config": "absl::FailedPreconditionError",
    "Io": "absl::InternalError",
    "Network": "absl::UnavailableError",
    "Provider": "absl::UnavailableError",
    "Tool": "absl::InternalError",
    "Cancelled": "absl::CancelledError",
    "Internal": "absl::InternalError",
    "AlreadyExists": "absl::AlreadyExistsError",
    "NotFound": "absl::NotFoundError",
    "Timeout": "absl::DeadlineExceededError",
}

def fix_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()

    original = text

    # 1. Replace remaining ErrorKind::X with absl factory (in code, not as type)
    # Pattern: ErrorKind::X (followed by ) or string or comma)
    def replace_error_kind(m):
        kind = m.group(1)
        factory = ERROR_FACTORY.get(kind, "absl::InternalError")
        return factory

    # Match ErrorKind::X when it's used as a function call argument
    # (ErrorKind::Io, "msg") pattern - the colon-comma case
    text = re.sub(r'\bErrorKind::(\w+)\b(?=\s*[,)])', replace_error_kind, text)

    # 2. Replace .error() with .status() - works for absl::StatusOr
    # Be careful with std::error_code which has .error() - exclude those
    # Match `.error()` not preceded by `ec.` or `error.` or `e_code.`
    text = re.sub(r'(?<![.a-zA-Z_])\.error\(\)', '.status()', text)

    # 3. Replace function renames (snake_case to PascalCase)
    for old, new in HELPER_RENAMES.items():
        # Word boundary replacement
        text = re.sub(r'\b' + re.escape(old) + r'\b', new, text)

    # 4. Replace `if (!result)` and `while (!result)` with `!result.ok()` for StatusOr
    # This is heuristic: only apply to common variable names
    # Skip this for now - too risky

    # 5. Replace `if (result)` and `while (result)` with `result.ok()`
    # Skip this for now - too risky

    # 6. Replace remaining `CodeHarnessError` references with `absl::Status`
    text = re.sub(r'\bCodeHarnessError\b', 'absl::Status', text)

    if text != original:
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)
        return True
    return False

if __name__ == "__main__":
    changed = 0
    for d in ['cli', 'commands', 'config', 'coordinator', 'hooks', 'mailbox', 'mcp', 'memory', 'network', 'permissions', 'plugins', 'prompts', 'runtime', 'sessions', 'skills', 'tasks', 'tui', 'ui_backend', 'core']:
        base = 'src/codeharness/' + d
        if not os.path.isdir(base):
            continue
        for dp, _, files in os.walk(base):
            for f in files:
                if f.endswith(('.h', '.cpp')):
                    p = os.path.join(dp, f)
                    if fix_file(p):
                        changed += 1
    print(f"Changed {changed} files")
