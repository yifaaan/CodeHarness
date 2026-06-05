#pragma once

#include "codeharness/network/http_client.h"
#include "codeharness/provider/provider.h"
#include "codeharness/provider/provider_config.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace codeharness
{

class OpenAIProvider final : public Provider
{
public:
    OpenAIProvider(ProviderConfig config,
                   std::vector<std::pair<std::string, std::string>> tool_descriptions = {});

    auto stream(std::span<const Message> messages, const ProviderEventSink& sink) -> Result<void> override;

private:
    ProviderConfig config_;
    std::vector<std::pair<std::string, std::string>> tool_descriptions_;
    network::HttpClient http_;
};

} // namespace codeharness
