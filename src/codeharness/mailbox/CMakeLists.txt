codeharness_add_static_module(codeharness_mailbox
    mailbox.cpp
    mailbox_tools.cpp
    message_consumer.cpp
    team_lifecycle.cpp
)

# 依赖关系：
#   codeharness_foundation (PUBLIC) — 核心类型：Result<T>, ensure_directory(), utc_timestamp_seconds()
#   codeharness_tools      (PRIVATE) — Tool 基类、ToolRegistry（只有 mailbox_tools 需要）
#   codeharness_tasks      (PRIVATE) — TaskManager（只有 mailbox_tools 的可选验证需要）
target_link_libraries(codeharness_mailbox
    PUBLIC
        codeharness_foundation
    PRIVATE
        codeharness_tools
        codeharness_tasks
)

# 日期库：用于 utc_timestamp_seconds() 生成 ISO 8601 时间戳
codeharness_link_first_existing(codeharness_mailbox PRIVATE date date::date)
# JSON 库：用于消息序列化和工具输入解析
codeharness_link_first_existing(codeharness_mailbox PRIVATE nlohmann_json nlohmann_json::nlohmann_json)

add_library(codeharness::mailbox ALIAS codeharness_mailbox)
