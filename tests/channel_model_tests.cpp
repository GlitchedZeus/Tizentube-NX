#include "tizentube_nx/core/channel_model.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

ttnx::core::BrowseResult video(std::string id, std::string title) {
    ttnx::core::BrowseResult result;
    result.kind = ttnx::core::BrowseResultKind::Video;
    result.id = std::move(id);
    result.title = std::move(title);
    return result;
}

ttnx::core::ChannelPage page_with(std::string browse_id, std::initializer_list<ttnx::core::BrowseResult> results) {
    ttnx::core::ChannelPage page;
    page.identity.browse_id = std::move(browse_id);
    page.metadata.title = "Parsed Channel";
    for (const auto& result : results) page.results.push_back(result);
    return page;
}

}  // namespace

int main() {
    using namespace ttnx::core;

    ChannelModel model;
    expect(model.state() == ChannelViewState::Idle, "Channel starts idle");

    ChannelIdentity a{"UCabcdefghijklmnopqrstuv", "Search Channel A"};
    const auto g1 = model.begin_load(a);
    expect(model.state() == ChannelViewState::Loading, "begin_load enters Loading");
    expect(model.identity().browse_id == a.browse_id, "requested identity is retained while loading");

    auto page = page_with(a.browse_id, {video("abcdefghijk", "Alpha")});
    expect(model.apply_page(g1, std::move(page)), "matching generation applies Channel page");
    expect(model.state() == ChannelViewState::Ready, "non-empty Channel enters Ready");
    expect(model.results().size() == 1, "normalized results retained");
    expect(model.metadata().title == "Parsed Channel", "parsed Channel metadata retained");

    const auto selected = browse_result_identity(model.results()[0]);
    expect(model.select(selected), "Channel selection accepts normalized identity");
    expect(model.selected_identity() == selected, "Channel selection is View-independent");
    model.clear_selection();
    expect(model.selected_identity().empty(), "Channel selection clears non-destructively");

    ChannelIdentity b{"UC1234567890123456789012", "Channel B"};
    const auto stale = model.begin_load(b);
    ChannelIdentity c{"UC2345678901234567890123", "Channel C"};
    const auto fresh = model.begin_load(c);
    expect(!model.apply_page(stale, page_with(b.browse_id, {video("bbbbbbbbbbb", "Stale")})),
           "stale Channel A/B completion cannot overwrite newer request");
    expect(model.apply_page(fresh, page_with(c.browse_id, {video("ccccccccccc", "Fresh")})),
           "newest Channel completion applies");
    expect(model.results().size() == 1 && model.results()[0].id == "ccccccccccc",
           "fresh Channel result wins");

    const auto g4 = model.begin_load(c);
    ChannelPage empty;
    empty.identity = c;
    expect(model.apply_page(g4, std::move(empty)), "valid empty Channel page applies");
    expect(model.state() == ChannelViewState::Empty, "valid empty Channel enters Empty");

    const auto g5 = model.begin_load(c);
    expect(model.fail(g5, std::string(800, 'x')), "current Channel failure applies");
    expect(model.state() == ChannelViewState::Error, "Channel failure enters Error");
    expect(model.error().size() == 512, "Channel error text is bounded");

    model.reset();
    expect(model.state() == ChannelViewState::Idle, "Channel reset returns Idle");
    expect(model.results().empty(), "Channel reset clears result state");

    if (failures == 0) {
        std::cout << "All Channel model tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
