# TUI library - port of Kimi TUI using FTXUI
# See plan: docs/plan/kimi-tui-port.md

find_package(ftxui CONFIG REQUIRED)

add_library(codeharness_tui
  TuiApp.cpp
  TuiState.cpp
  EventRouter.cpp
  Theme/Theme.cpp
  Theme/TerminalTheme.cpp
  Components/ChatPane.cpp
  Components/InputField.cpp
  Components/MessageEntry.cpp
  Components/ToolCallCard.cpp
  Components/ApprovalPanel.cpp
  Components/QuestionDialog.cpp
  Components/StatusBar.cpp
  Components/SidePanel.cpp
  Components/CompactionIndicator.cpp
  Components/HelpDialog.cpp
  Components/SessionPicker.cpp
  Components/SettingsDialog.cpp
  Components/Spinner.cpp
  Renderers/MarkdownRenderer.cpp
  Renderers/CodeHighlighter.cpp
  Renderers/DiffRenderer.cpp
  Utils/TerminalUtils.cpp
  Utils/InputHistory.cpp
  Utils/KeyBindings.cpp
  Utils/SlashCommands.cpp
)

target_include_directories(codeharness_tui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(codeharness_tui PUBLIC cxx_std_20)
target_link_libraries(codeharness_tui PRIVATE
  codeharness
  ftxui::screen
  ftxui::dom
  ftxui::component
)