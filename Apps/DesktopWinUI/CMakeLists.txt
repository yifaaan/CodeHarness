cmake_minimum_required(VERSION 3.25)

if(NOT WIN32)
  message(FATAL_ERROR "CodeHarness Desktop WinUI can only be built on Windows.")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "CodeHarness Desktop WinUI requires MSVC and the Visual Studio generator.")
endif()

set(CODEHARNESS_WINDOWS_APP_SDK_VERSION "2.2.0" CACHE STRING "Microsoft.WindowsAppSDK NuGet version for the WinUI desktop app")
set(CODEHARNESS_WINDOWS_APP_SDK_FOUNDATION_VERSION "2.1.0" CACHE STRING "Microsoft.WindowsAppSDK.Foundation NuGet version restored transitively for bootstrap linking")
set(CODEHARNESS_CPPWINRT_VERSION "3.0.260520.1" CACHE STRING "Microsoft.Windows.CppWinRT NuGet version for the WinUI desktop app")
if(NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
  set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "10.0.26100.0")
endif()

set(DESKTOP_XAML_FILES
  App.xaml
  MainWindow.xaml
  Views/ShellPage.xaml
  Views/ChatPage.xaml
  Views/SettingsPage.xaml
  Controls/Sidebar.xaml
  Controls/ComposerBox.xaml
  Controls/PermissionDialog.xaml
  Controls/ToolCallView.xaml
  Themes/Colors.xaml
  Themes/Typography.xaml
  Themes/Controls.xaml
)

set(DESKTOP_IDL_FILES
	MainWindow.idl
	XamlMetaDataProvider.idl
	Views/ShellPage.idl
	Views/ChatPage.idl
	Views/SettingsPage.idl
  Controls/Sidebar.idl
  Controls/ComposerBox.idl
  Controls/PermissionDialog.idl
  Controls/ToolCallView.idl
)

set(DESKTOP_SOURCE_FILES
	main.cpp
	App.xaml.cpp
	MainWindow.xaml.cpp
	XamlMetaDataProvider.cpp
	Services/DesktopCoreService.cpp
	Services/DialogService.cpp
	Services/UiDispatcher.cpp
  ViewModels/ChatViewModel.cpp
  ViewModels/MessageViewModel.cpp
  ViewModels/SessionListViewModel.cpp
  Views/ShellPage.xaml.cpp
  Views/ChatPage.xaml.cpp
  Views/SettingsPage.xaml.cpp
  Controls/Sidebar.xaml.cpp
  Controls/ComposerBox.xaml.cpp
  Controls/PermissionDialog.xaml.cpp
  Controls/ToolCallView.xaml.cpp
  Resources.rc
)

set(DESKTOP_HEADER_FILES
	App.xaml.h
	MainWindow.xaml.h
	XamlMetaDataProvider.h
	Services/DesktopCoreService.h
	Services/DialogService.h
	Services/UiDispatcher.h
  ViewModels/ChatViewModel.h
  ViewModels/MessageViewModel.h
  ViewModels/SessionListViewModel.h
  Views/ShellPage.xaml.h
  Views/ChatPage.xaml.h
  Views/SettingsPage.xaml.h
  Controls/Sidebar.xaml.h
  Controls/ComposerBox.xaml.h
  Controls/PermissionDialog.xaml.h
  Controls/ToolCallView.xaml.h
)

set(DESKTOP_RESOURCE_FILES
  Package.appxmanifest
  app.manifest
)
foreach(desktop_config Debug Release MinSizeRel RelWithDebInfo)
	file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/codeharness_desktop.dir/${desktop_config}")
	file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/codeharness_desktop.dir/${desktop_config}/Generated Files")
endforeach()

function(codeharness_add_xaml_include_shim shim_file generated_file)
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/codeharness_desktop.dir/$<CONFIG>/Generated Files/${shim_file}"
    CONTENT "#pragma once\n#include \"${generated_file}\"\n"
  )
endfunction()

codeharness_add_xaml_include_shim("Views.ShellPage.xaml.g.h" "Views/ShellPage.xaml.g.h")
codeharness_add_xaml_include_shim("Views.ChatPage.xaml.g.h" "Views/ChatPage.xaml.g.h")
codeharness_add_xaml_include_shim("Views.SettingsPage.xaml.g.h" "Views/SettingsPage.xaml.g.h")
codeharness_add_xaml_include_shim("Controls.Sidebar.xaml.g.h" "Controls/Sidebar.xaml.g.h")
codeharness_add_xaml_include_shim("Controls.ComposerBox.xaml.g.h" "Controls/ComposerBox.xaml.g.h")
codeharness_add_xaml_include_shim("Controls.PermissionDialog.xaml.g.h" "Controls/PermissionDialog.xaml.g.h")
codeharness_add_xaml_include_shim("Controls.ToolCallView.xaml.g.h" "Controls/ToolCallView.xaml.g.h")

set(DESKTOP_XAML_GENERATED_CPP "${CMAKE_CURRENT_BINARY_DIR}/codeharness_desktop.dir/$<CONFIG>/Generated Files/CodeHarnessDesktop.XamlGenerated.cpp")
file(GENERATE
  OUTPUT "${DESKTOP_XAML_GENERATED_CPP}"
  CONTENT [[#include <unknwn.h>
#include <winrt/base.h>
#undef GetCurrentTime
#define DISABLE_XAML_GENERATED_MAIN

#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "XamlMetaDataProvider.h"
#include "Views/ShellPage.xaml.h"
#include "Views/ChatPage.xaml.h"
#include "Views/SettingsPage.xaml.h"
#include "Controls/Sidebar.xaml.h"
#include "Controls/ComposerBox.xaml.h"
#include "Controls/PermissionDialog.xaml.h"
#include "Controls/ToolCallView.xaml.h"

#include "MainWindow.g.cpp"
#include "module.g.cpp"
#include "XamlTypeInfo.g.cpp"
#include "XamlTypeInfo.Impl.g.cpp"
#include "XamlLibMetadataProvider.g.cpp"
#include "Views.ShellPage.g.cpp"
#include "Views.ChatPage.g.cpp"
#include "Views.SettingsPage.g.cpp"
#include "Controls.Sidebar.g.cpp"
#include "Controls.ComposerBox.g.cpp"
#include "Controls.PermissionDialog.g.cpp"
#include "Controls.ToolCallView.g.cpp"
]]
)

add_executable(codeharness_desktop WIN32
  ${DESKTOP_SOURCE_FILES}
  ${DESKTOP_HEADER_FILES}
  ${DESKTOP_XAML_FILES}
  ${DESKTOP_IDL_FILES}
  ${DESKTOP_RESOURCE_FILES}
)

target_sources(codeharness_desktop PRIVATE "${DESKTOP_XAML_GENERATED_CPP}")

target_compile_features(codeharness_desktop PRIVATE cxx_std_20)
target_include_directories(codeharness_desktop PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  "${CMAKE_CURRENT_BINARY_DIR}/codeharness_desktop.dir/$<CONFIG>/Generated Files"
)
target_compile_definitions(codeharness_desktop PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  CODEHARNESS_WINDOWS_APP_SDK_MAJOR_MINOR=0x00020000
  WINRT_FORCE_INCLUDE_MAINWINDOW_XAML_G_H
  WINRT_FORCE_INCLUDE_SHELLPAGE_XAML_G_H
  WINRT_FORCE_INCLUDE_CHATPAGE_XAML_G_H
  WINRT_FORCE_INCLUDE_SETTINGSPAGE_XAML_G_H
  WINRT_FORCE_INCLUDE_SIDEBAR_XAML_G_H
  WINRT_FORCE_INCLUDE_COMPOSERBOX_XAML_G_H
  WINRT_FORCE_INCLUDE_PERMISSIONDIALOG_XAML_G_H
  WINRT_FORCE_INCLUDE_TOOLCALLVIEW_XAML_G_H
)
target_compile_options(codeharness_desktop PRIVATE /FS)
set(CODEHARNESS_WINDOWS_APP_SDK_NUGET_ROOT "$ENV{USERPROFILE}/.nuget/packages" CACHE PATH "NuGet package root used to locate Windows App SDK native libraries")
set(CODEHARNESS_WINDOWS_APP_SDK_NATIVE_PLATFORM "${CMAKE_VS_PLATFORM_NAME}")
if(CODEHARNESS_WINDOWS_APP_SDK_NATIVE_PLATFORM STREQUAL "Win32")
  set(CODEHARNESS_WINDOWS_APP_SDK_NATIVE_PLATFORM "x86")
endif()
string(TOLOWER "${CODEHARNESS_WINDOWS_APP_SDK_NATIVE_PLATFORM}" CODEHARNESS_WINDOWS_APP_SDK_NATIVE_PLATFORM)
set(CODEHARNESS_WINDOWS_APP_RUNTIME_BOOTSTRAP_LIB
  "${CODEHARNESS_WINDOWS_APP_SDK_NUGET_ROOT}/microsoft.windowsappsdk.foundation/${CODEHARNESS_WINDOWS_APP_SDK_FOUNDATION_VERSION}/lib/native/${CODEHARNESS_WINDOWS_APP_SDK_NATIVE_PLATFORM}/Microsoft.WindowsAppRuntime.Bootstrap.lib"
  CACHE FILEPATH "Windows App SDK bootstrap import library"
)
if(NOT EXISTS "${CODEHARNESS_WINDOWS_APP_RUNTIME_BOOTSTRAP_LIB}")
  message(FATAL_ERROR
    "Windows App SDK bootstrap import library was not found at '${CODEHARNESS_WINDOWS_APP_RUNTIME_BOOTSTRAP_LIB}'. "
    "Restore the Microsoft.WindowsAppSDK NuGet package or set CODEHARNESS_WINDOWS_APP_RUNTIME_BOOTSTRAP_LIB.")
endif()
target_link_libraries(codeharness_desktop PRIVATE
  codeharness
  "${CODEHARNESS_WINDOWS_APP_RUNTIME_BOOTSTRAP_LIB}"
  windowsapp
)

set(DESKTOP_XAML_VS_SETTINGS "Generator=MSBuild:Compile;XamlRuntime=WinUI;SubType=Designer")
set_source_files_properties(App.xaml PROPERTIES
  VS_TOOL_OVERRIDE "ApplicationDefinition"
  VS_SETTINGS "${DESKTOP_XAML_VS_SETTINGS}"
)
foreach(xaml_file IN LISTS DESKTOP_XAML_FILES)
  if(NOT xaml_file STREQUAL "App.xaml")
    set_source_files_properties(${xaml_file} PROPERTIES
      VS_TOOL_OVERRIDE "Page"
      VS_SETTINGS "${DESKTOP_XAML_VS_SETTINGS}"
    )
  endif()
endforeach()
set(DESKTOP_GENERATED_FILES_DIR "codeharness_desktop.dir\\$(Configuration)\\Generated Files\\")
set(DESKTOP_IDL_VS_SETTINGS "OutputDirectory=$(ProjectDir)$(IntDir);HeaderFileName=%(Filename).h;TypeLibraryName=%(Filename).tlb;MetadataFileName=%(Filename).winmd")
foreach(idl_file IN LISTS DESKTOP_IDL_FILES)
  set_source_files_properties(${idl_file} PROPERTIES
    VS_TOOL_OVERRIDE "Midl"
    VS_SETTINGS "${DESKTOP_IDL_VS_SETTINGS}"
  )
endforeach()
set_source_files_properties(Package.appxmanifest PROPERTIES VS_TOOL_OVERRIDE "AppxManifest")

set_target_properties(codeharness_desktop PROPERTIES
  OUTPUT_NAME "CodeHarnessDesktop"
  VS_PACKAGE_REFERENCES "Microsoft.WindowsAppSDK_${CODEHARNESS_WINDOWS_APP_SDK_VERSION};Microsoft.Windows.CppWinRT_${CODEHARNESS_CPPWINRT_VERSION}"
  VS_GLOBAL_AppxPackage "false"
  VS_GLOBAL_AppxPackageSigningEnabled "false"
  VS_GLOBAL_CppWinRTAddXamlMetaDataProviderIdl "false"
  VS_GLOBAL_CppWinRTOptimized "true"
  VS_GLOBAL_CppWinRTRootNamespaceAutoMerge "true"
  VS_GLOBAL_CppWinRTGenerateWindowsMetadata "true"
  VS_GLOBAL_CppWinRTUsePrefixes "true"
  VS_GLOBAL_ForceImportAfterCppTargets "${CMAKE_CURRENT_SOURCE_DIR}\\CodeHarnessDesktop.Xaml.targets"
  VS_GLOBAL_GeneratedFilesDir "${DESKTOP_GENERATED_FILES_DIR}"
  VS_GLOBAL_IsNativeLanguage "true"
  VS_GLOBAL_ManagedAssembly "false"
  VS_GLOBAL_MinimalCoreWin "true"
  VS_GLOBAL_OutDir "$(SolutionDir)$(Configuration)\\"
  VS_GLOBAL_PreferredToolArchitecture "x64"
  VS_GLOBAL_RootNamespace "CodeHarness.Desktop"
  VS_GLOBAL_TargetPlatformIdentifier "Windows"
  VS_GLOBAL_TargetPlatformMinVersion "10.0.19041.0"
  VS_GLOBAL_TargetPlatformVersion "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
  VS_GLOBAL_XamlGeneratedOutputPath "${DESKTOP_GENERATED_FILES_DIR}"
  VS_GLOBAL_WindowsAppSdkBootstrapInitialize "false"
  VS_GLOBAL_WindowsPackageType "None"
  VS_GLOBAL_WinMDAssembly "true"
  VS_GLOBAL_XamlLanguage "CppWinRT"
  VS_GLOBAL_XamlNamespace "Microsoft.UI.Xaml"
  VS_GLOBAL_XamlTypeInfoProviderName "XamlMetaDataProvider"
)





