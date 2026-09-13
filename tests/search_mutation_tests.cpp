#include "tizentube_nx/youtube/search_response.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool contains_result(const ttnx::core::BrowsePage& page, const std::string& id) {
    for (const auto& result : page.results) {
        if (result.id == id) return true;
    }
    return false;
}

std::string wrap_primary(std::string body) {
    return "{\"contents\":{\"twoColumnSearchResultsRenderer\":{\"primaryContents\":" +
           std::move(body) + "}}}";
}

}  // namespace

int main() {
    using ttnx::youtube::parse_scoped_search_response;

    const std::vector<std::string> malformed = {
        "",
        "{",
        "[",
        "{\"contents\":[}",
        "{\"x\":\"\\uZZZZ\"}",
        "{\"x\":01}",
        "{\"x\":tru}",
        "{\"x\":\"unterminated}",
    };
    for (const auto& input : malformed) {
        const auto parsed = parse_scoped_search_response(input);
        expect(!parsed.page.has_value(), "malformed mutation is rejected without creating a page");
    }

    const std::string valid = wrap_primary(R"JSON({
      "sectionListRenderer":{"contents":[{"itemSectionRenderer":{"contents":[
        {"videoRenderer":{"videoId":"TRUNCATE001","title":{"simpleText":"Truncate me"}}}
      ]}}]}
    })JSON");
    for (std::size_t cut = 1; cut < valid.size(); cut += 17) {
        const auto parsed = parse_scoped_search_response(std::string_view(valid).substr(0, cut));
        expect(!parsed.page.has_value(), "truncated valid fixture is rejected without crashing");
    }
    expect(parse_scoped_search_response(valid).page.has_value(), "full valid fixture still parses after truncation corpus");

    std::string deep = "{\"contents\":{";
    for (int i = 0; i < 140; ++i) deep += "\"x\":{";
    deep += "\"twoColumnSearchResultsRenderer\":{\"primaryContents\":{}}";
    for (int i = 0; i < 140; ++i) deep += "}";
    deep += "}}";
    expect(!parse_scoped_search_response(deep).page.has_value(),
           "excessive JSON nesting hits the depth bound");

    std::string huge_string = "{\"padding\":\"";
    huge_string.append(8 * 1024 * 1024, 'x');
    huge_string += "\"}";
    expect(!parse_scoped_search_response(huge_string).page.has_value(),
           "giant string exceeding response bound is rejected");

    std::uint32_t state = 0x13579bdfu;
    for (int i = 0; i < 32; ++i) {
        state = state * 1664525u + 1013904223u;
        const auto suffix = std::to_string(state);
        const auto fixture = wrap_primary(
            "{\"sectionListRenderer\":{\"contents\":[{\"itemSectionRenderer\":{\"contents\":[{\"futureRenderer" +
            suffix + "\":{\"videoId\":\"UNKNOWN" + std::to_string(i) +
            "\",\"title\":{\"simpleText\":\"Unknown\"}}}]}}]}}"
        );
        const auto parsed = parse_scoped_search_response(fixture);
        expect(parsed.page.has_value(), "unknown renderer mutation keeps structurally valid Search page usable");
        if (parsed.page) {
            expect(parsed.page->results.empty(), "random unknown renderer name never becomes a result");
        }
    }

    for (int i = 0; i < 24; ++i) {
        state = state * 1664525u + 1013904223u;
        const auto fake_id = "OUT" + std::to_string(state);
        const std::string fixture =
            "{\"header" + std::to_string(i) + "\":{\"videoRenderer\":{\"videoId\":\"" +
            fake_id + "\",\"title\":{\"simpleText\":\"Outside\"}}},"
            "\"contents\":{\"twoColumnSearchResultsRenderer\":{\"primaryContents\":{"
            "\"sectionListRenderer\":{\"contents\":[]}}}}}";
        const auto parsed = parse_scoped_search_response(fixture);
        expect(parsed.page.has_value(), "out-of-scope renderer mutation keeps recognized Search payload usable");
        if (parsed.page) {
            expect(!contains_result(*parsed.page, fake_id),
                   "randomly placed out-of-scope videoRenderer cannot escape");
        }
    }

    for (const std::string blocked_key : {
             "adSlotRenderer", "promotedShelfRenderer", "reelShelfRenderer",
             "shortsLockupViewModel", "productRenderer", "merchandiseShelfRenderer"}) {
        const auto fixture = wrap_primary(
            "{\"sectionListRenderer\":{\"contents\":[{\"itemSectionRenderer\":{\"contents\":[{\"" +
            blocked_key + "\":{\"nested\":{\"videoRenderer\":{\"videoId\":\"BLOCKED0001\","
            "\"title\":{\"simpleText\":\"Nested known renderer\"}}}}}]}}]}}"
        );
        const auto parsed = parse_scoped_search_response(fixture);
        expect(parsed.page.has_value(), "blocked-subtree mutation keeps valid page usable");
        if (parsed.page) {
            expect(!contains_result(*parsed.page, "BLOCKED0001"),
                   "blocked subtree remains opaque even with nested known renderer name");
        }
    }

    if (failures == 0) {
        std::cout << "All deterministic Search mutation tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
