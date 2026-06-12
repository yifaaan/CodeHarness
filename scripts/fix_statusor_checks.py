import os, re

# Fix patterns like:
#   if (!result)  ->  if (!result.ok())
#   if (result)   ->  if (result.ok())
#   result.error()  ->  result.status()

# Variables that hold absl::StatusOr are typically:
# - result, response, parsed, parse, value, val
# - names ending in _result, _status
# - return values from functions returning StatusOr

# Common variable names that are StatusOr
STATUSOR_VAR_NAMES = {
    'result', 'response', 'parsed', 'parse', 'value', 'val',
    'parsed_input', 'parsed_config', 'parsed_state',
    'workdir', 'parsed_path', 'resolved', 'auth',
    'output', 'rule', 'permission', 'cached', 'loaded', 'loaded_config',
    'loaded_credentials', 'deserialized', 'decoded',
    'config', 'credentials', 'session',
    'updated', 'created', 'saved', 'fetched', 'retrieved',
    'all', 'current', 'matched', 'matches', 'candidate',
    'agent', 'member', 'subagent', 'team', 'task', 'command',
    'tools', 'providers', 'commands', 'items', 'entries',
    'enabled', 'disabled', 'computed',
}

# This is a heuristic approach: find common variable names in `if (!name)` or `if (name)`
# and convert them

def fix_file(path):
    with open(path, 'r', encoding='utf-8') as fp:
        text = fp.read()

    original = text

    # 1. Replace `if (!name)` and `while (!name)` with `if (!name.ok())` for known StatusOr names
    for name in STATUSOR_VAR_NAMES:
        # Match `if (!name)` not followed by `.ok(`
        pattern = r'\b(if|while)\s*\(\s*!' + re.escape(name) + r'\b\)(?!\s*\.ok)'
        text = re.sub(pattern, r'\1(!' + name + '.ok())', text)

        # Match `if (name)` not followed by `.ok(`
        # Be careful - bool variables shouldn't be changed
        # Only do this for known StatusOr names where they have an `.ok()` call somewhere nearby
        # Skip for now to be safe

    # 2. Replace `name.error()` with `name.status()` for known StatusOr names
    for name in STATUSOR_VAR_NAMES:
        pattern = r'\b' + re.escape(name) + r'\.error\(\)'
        text = re.sub(pattern, name + '.status()', text)

    if text != original:
        with open(path, 'w', encoding='utf-8') as fp:
            fp.write(text)
        return True
    return False


if __name__ == "__main__":
    changed = 0
    for d in ['cli', 'commands', 'config', 'coordinator', 'hooks', 'mailbox', 'mcp', 'memory', 'network', 'permissions', 'plugins', 'prompts', 'runtime', 'sessions', 'skills', 'tasks', 'tui', 'ui_backend']:
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
