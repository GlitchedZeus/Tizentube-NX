#include "tizentube_nx/core/home_model.hpp"

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

ttnx::core::HomePage page_with(std::initializer_list<ttnx::core::BrowseResult> results) {
    ttnx::core::HomePage page;
    ttnx::core::HomeSection section;
    section.title = "Home";
    for (const auto& result : results) {
        page.results.push_back(result);
        section.results.push_back(result);
    }
    if (!section.results.empty()) page.sections.push_back(std::move(section));
    return page;
}

}  // namespace

int main() {
    using ttnx::core::HomeModel;
    using ttnx::core::HomeViewState;
    using ttnx::core::browse_result_identity;

    HomeModel model;
    expect(model.state() == HomeViewState::Idle, "Home starts idle");

    const auto generation_one = model.begin_load();
    expect(model.state() == HomeViewState::Loading, "begin_load enters Loading");
    expect(model.apply_page(generation_one, page_with({video("A", "Alpha"), video("B", "Beta")})),
           "matching generation applies Home page");
    expect(model.state() == HomeViewState::Ready, "non-empty Home page enters Ready");
    expect(model.results().size() == 2, "Home page retains normalized results");

    const auto identity_a = browse_result_identity(model.results()[0]);
    expect(model.select(identity_a), "select accepts visible normalized identity");
    expect(model.selected_identity() == identity_a, "selected identity stored independently of Views");
    model.clear_selection();
    expect(model.selected_identity().empty(), "clear_selection collapses Home detail state");

    expect(model.select(identity_a), "selection can be reopened");
    const auto generation_two = model.begin_load();
    expect(model.results().size() == 2, "refresh keeps old Home results visible while loading");
    expect(model.selected_identity() == identity_a,
           "refresh keeps old selection until replacement page is accepted");
    expect(model.apply_page(generation_two, page_with({video("B", "Beta refreshed")})),
           "refresh page applies");
    expect(model.selected_identity().empty(),
           "selection clears when refreshed Home page no longer contains identity");

    const auto generation_three = model.begin_load();
    const auto generation_four = model.begin_load();
    expect(!model.apply_page(generation_three, page_with({video("STALE", "Stale")})),
           "stale Home generation cannot overwrite a newer load");
    expect(model.apply_page(generation_four, page_with({video("FRESH", "Fresh")})),
           "current Home generation applies");
    expect(model.results().size() == 1 && model.results()[0].id == "FRESH",
           "stale page did not leak into current Home state");

    const auto generation_five = model.begin_load();
    expect(model.fail(generation_five, "safe Home failure"), "current failure applies");
    expect(model.state() == HomeViewState::Error, "Home failure enters Error");
    expect(model.results().size() == 1 && model.results()[0].id == "FRESH",
           "Home failure preserves last good results");
    expect(model.error() == "safe Home failure", "Home keeps bounded safe error text");

    const auto generation_six = model.begin_load();
    ttnx::core::HomePage empty;
    expect(model.apply_page(generation_six, std::move(empty)), "empty Home page applies");
    expect(model.state() == HomeViewState::Empty, "empty Home page enters Empty");

    model.reset();
    expect(model.state() == HomeViewState::Idle, "reset returns Home to Idle");
    expect(model.results().empty(), "reset clears Home results");

    if (failures == 0) {
        std::cout << "All Home model tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
