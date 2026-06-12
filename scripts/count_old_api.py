import os
total = 0
for d in ['cli', 'commands', 'config', 'coordinator', 'hooks', 'mailbox', 'mcp', 'memory', 'network', 'permissions', 'plugins', 'prompts', 'runtime', 'sessions', 'skills', 'tasks', 'tui', 'ui_backend']:
    base = 'src/codeharness/' + d
    if not os.path.isdir(base):
        continue
    for dp, _, files in os.walk(base):
        for f in files:
            if f.endswith(('.h', '.cpp')):
                p = os.path.join(dp, f)
                with open(p, 'r', encoding='utf-8') as fp: text = fp.read()
                c = text.count('Result<') + text.count('fail<') + text.count('ErrorKind') + text.count('nonstd::make_unexpected')
                if c > 0:
                    print(d + '/' + f, c)
                    total += c
print('TOTAL:', total)
