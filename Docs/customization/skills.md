# Agent Skills

Skills 是可加载的提示词和工作流片段。CodeHarness 会把可由用户调用的 Skill 注册为斜杠命令，也会提供 `skill` 工具让模型调用 inline 类型 Skill。

## Skill 存放位置

默认用户级 Skill 目录：

```text
<config_dir>/skills
```

项目级 Skill 是否加载由 `allow_project_skills` 控制，默认开启。

## Skill 定义

源码中的 Skill 定义包含：

| 字段 | 说明 |
| --- | --- |
| `name` | Skill 名称 |
| `description` | 描述 |
| `content` | 注入 prompt 的正文 |
| `source` | 来源 |
| `command_name` | 可选斜杠命令名 |
| `display_name` | 可选展示名 |
| `aliases` | 别名 |
| `user_invocable` | 是否允许用户调用 |
| `disable_model_invocation` | 是否禁止模型调用 |
| `model` | 可选指定 profile |
| `argument_hint` | 参数提示 |

## 调用 Skill

用户可以通过动态斜杠命令调用：

```text
/skill-name 附加参数
```

如果 Skill 设置了 `command_name`，则使用该命令名。命令内容会渲染为 prompt 并提交给 agent loop。

模型可以通过 `skill` 工具调用允许 model invocation 的 Skill。

## 参数占位符

Skill 内容支持：

```text
${ARGUMENTS}
$ARGUMENTS
```

如果正文没有占位符，但用户提供了附加参数，CodeHarness 会在渲染后的 prompt 末尾追加 `Arguments: ...`。
