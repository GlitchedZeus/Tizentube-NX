// SPDX-License-Identifier: GPL-3.0-only
// Adapted 2026-10-11 from mirusu400/switch-newpipe (GPL-3.0),
// https://github.com/mirusu400/switch-newpipe/tree/a54b009f6d046a8ea7585e86c3c7ed41c4a57984
// Original copyright remains with its contributors. This file is a modified
// adapter; see docs/THIRD_PARTY_MEDIA.md. No extracted media URLs are logged.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ttnx::playback {

struct UmpParseResult {
    std::vector<uint8_t> media_data;
    std::vector<int> stream_protection_statuses;
    std::string redirect_url;
    bool complete = true;
};

UmpParseResult parse_ump_response(const uint8_t* data, size_t size);

std::string set_url_query_parameter(
    const std::string& url,
    const std::string& key,
    const std::string& value);

std::optional<std::string> find_url_query_parameter(
    const std::string& url,
    const std::string& key);

std::string build_ump_request_url(
    const std::string& base_url,
    uint64_t range_start,
    uint64_t range_end,
    int request_number);

std::string merge_ump_redirect_parameters(
    const std::string& redirect_url,
    const std::string& request_url);

}  // namespace ttnx::playback
