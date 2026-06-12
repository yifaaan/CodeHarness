# 数据路径

CodeHarness 把配置和运行数据分开存放。默认情况下，配置在 `~/.codeharness`，运行数据在 `~/.codeharness/data`。

## 核心路径

| 用途 | 默认路径 | 覆盖方式 |
| --- | --- | --- |
| 配置目录 | `~/.codeharness` | `CODEHARNESS_CONFIG_DIR` |
| 数据目录 | `<config_dir>/data` | `CODEHARNESS_DATA_DIR` |
| Skills | `<config_dir>/skills` | 跟随配置目录 |
| Plugins | `<config_dir>/plugins` | 跟随配置目录 |
| Agents | `<config_dir>/agents` | 跟随配置目录 |
| 会话 | `<data_dir>/sessions` | 跟随数据目录 |
| Memory | `<data_dir>/memory` | `CODEHARNESS_MEMORY_ROOT` |
| 任务 | `<data_dir>/tasks` | 跟随数据目录 |
| 工具产物 | `<data_dir>/tool_artifacts` | 跟随数据目录 |

## 配置文件

- `settings.json`：主配置文件。
- `credentials.json`：API key 等本地凭据。

凭据不会被 `settings.json` 的序列化逻辑写回，避免把 API key 混入普通配置。

## 项目级配置发现

CodeHarness 会从当前工作目录向上查找 `.codeharness` 目录。找到项目级 `.codeharness/settings.json` 时，可用于未来扩展项目配置。当前全局配置仍是主要入口。

## 清理数据

清理前先确认目录变量解析结果，避免误删：

```powershell
codeharness --version
$env:CODEHARNESS_DATA_DIR
```

会话、任务和记忆都属于运行数据；删除后不会影响可执行文件，但会影响历史恢复和任务查询。
