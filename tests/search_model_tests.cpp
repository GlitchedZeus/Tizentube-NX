#include "tizentube_nx/core/search_model.hpp"

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

}  // namespace

int main() {
    using namespace ttnx::core;

    SearchModel model;
    expect(model.state() == SearchViewState::Idle, "Search model starts idle");
    expect(model.results().empty(), "Search model starts with no results");

    const auto gen_a = model.begin_query("query A");
    expect(gen_a != 0, "begin query returns a generation token");
    expect(model.state() == SearchViewState::Loading, "begin query enters loading state");
    expect(model.query() == "query A", "query text is retained for presentation/controller use");

    BrowsePage first;
    first.results = {video("VIDEO000001", "One"), video("VIDEO000002", "Two")};
    first.continuation = "NEXT-A";
    expect(model.apply_first_page(gen_a, first), "matching first page is accepted");
    expect(model.state() == SearchViewState::Ready, "non-empty first page enters ready state");
    expect(model.results().size() == 2, "first page populates normalized result list");
    expect(model.can_load_more(), "continuation makes loading-more available");

    expect(model.select("video:VIDEO000002"), "selection uses stable normalized identity");
    expect(model.selected_identity() == "video:VIDEO000002", "selected identity is retained");
    expect(!model.select("video:DOESNOTEXIST"), "selection rejects unknown identity");

    expect(model.begin_load_more(gen_a), "ready page can enter loading-more state");
    expect(model.state() == SearchViewState::LoadingMore, "loading-more state is explicit");

    BrowsePage next;
    next.results = {video("VIDEO000002", "Duplicate Two"), video("VIDEO000003", "Three")};
    next.continuation.clear();
    expect(model.apply_continuation(gen_a, next), "matching continuation page is accepted");
    expect(model.state() == SearchViewState::Ready, "continuation returns to ready state");
    expect(model.results().size() == 3, "continuation merge deduplicates by stable identity");
    expect(model.selected_identity() == "video:VIDEO000002", "valid selection survives continuation merge");
    expect(!model.can_load_more(), "missing continuation disables loading more");

    const auto gen_b = model.begin_query("query B");
    expect(gen_b > gen_a, "new query receives a newer generation");
    expect(model.results().empty(), "new query clears old results");
    expect(model.selected_identity().empty(), "new query clears old selection");
    expect(model.continuation().empty(), "new query clears old continuation");
    expect(!model.apply_first_page(gen_a, first), "stale query A completion is rejected after query B starts");
    expect(model.state() == SearchViewState::Loading, "stale completion cannot change current loading state");

    BrowsePage empty;
    empty.continuation = "EMPTY-NEXT";
    expect(model.apply_first_page(gen_b, empty), "empty first page is accepted");
    expect(model.state() == SearchViewState::Empty, "empty first page has explicit empty state");
    expect(model.can_load_more(), "empty page may still expose a continuation");
    expect(model.begin_load_more(gen_b), "empty state can load a valid continuation");

    expect(model.fail(gen_b, "temporary failure"), "loading-more failure enters error state");
    expect(model.state() == SearchViewState::Error, "error state is explicit");
    expect(model.error() == "temporary failure", "error text is retained for presentation");
    expect(model.can_load_more(), "failed loading-more keeps continuation available for deliberate retry");
    expect(model.begin_load_more(gen_b), "retry can re-enter loading-more state");

    BrowsePage recovered;
    recovered.results = {video("RECOVER0001", "Recovered")};
    expect(model.apply_continuation(gen_b, recovered), "retry continuation can recover from error");
    expect(model.state() == SearchViewState::Ready, "successful retry returns to ready");
    expect(model.error().empty(), "successful retry clears error text");

    const auto gen_empty = model.begin_query("");
    expect(model.state() == SearchViewState::Idle, "empty query leaves model idle rather than loading");
    expect(!model.apply_first_page(gen_empty, first), "idle empty query cannot accept Search results");

    const auto before_reset = model.generation();
    model.reset();
    expect(model.generation() > before_reset, "reset invalidates in-flight generation tokens");
    expect(model.state() == SearchViewState::Idle, "reset returns to idle");
    expect(model.query().empty() && model.results().empty() && model.error().empty(),
           "reset clears presentation state");

    if (failures == 0) {
        std::cout << "All offline Search model tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
