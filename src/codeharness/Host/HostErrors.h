#pragma once

#include <exception>
#include <filesystem>
#include <string>

namespace codeharness::host
{

	class HostError : public std::exception
	{
	  public:
		explicit HostError(std::string message) : message(std::move(message)) {}

		const char *what() const noexcept override
		{
			return message.c_str();
		}

		const std::string &GetMessage() const
		{
			return message;
		}

	  private:
		std::string message;
	};

	class HostValueError : public HostError
	{
	  public:
		explicit HostValueError(std::string message) : HostError(std::move(message)) {}
	};

	class HostFileExistsError : public HostError
	{
	  public:
		explicit HostFileExistsError(std::filesystem::path path)
			: HostError("file already exists: " + path.string()), filePath(std::move(path)) {}

		const std::filesystem::path &GetPath() const
		{
			return filePath;
		}

	  private:
		std::filesystem::path filePath;
	};

	class HostShellNotFoundError : public HostError
	{
	  public:
		explicit HostShellNotFoundError(std::string message) : HostError(std::move(message)) {}
	};

	class HostFileNotFoundError : public HostError
	{
	  public:
		explicit HostFileNotFoundError(std::filesystem::path path)
			: HostError("file not found: " + path.string()), filePath(std::move(path)) {}

		const std::filesystem::path &GetPath() const
		{
			return filePath;
		}

	  private:
		std::filesystem::path filePath;
	};

	class HostPermissionError : public HostError
	{
	  public:
		explicit HostPermissionError(std::filesystem::path path)
			: HostError("permission denied: " + path.string()), filePath(std::move(path)) {}

		const std::filesystem::path &GetPath() const
		{
			return filePath;
		}

	  private:
		std::filesystem::path filePath;
	};

} // namespace codeharness::host