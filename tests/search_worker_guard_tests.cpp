#include "tizentube_nx/youtube/search_flow.hpp"
#include "tizentube_nx/youtube/search_worker_guard.hpp"

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

ttnx::youtube::GuestSession session() {
    ttnx::youtube::GuestSession value;
    value.client_name = "WEB";
    value.client_version = "2.20260901.00.00";
    value.visitor_data = "visitor-test";
    value.language = "en";
    value.region = "CA";
    value.timezone = "America/Toronto";
    value.user_agent = "TizenTube-NX-Test";
    return value;
}

class ThrowingHttpClient final : public ttnx::net::HttpClient {
public:
    enum class Failure { BadAlloc, Runtime } failure{Failure::BadAlloc};

    ttnx::net::HttpResult perform(const ttnx::net::HttpRequest&) override {
        if (failure == Failure::BadAlloc) throw std::bad_alloc{};
        throw std::runtime_error("sensitive internal diagnostic must not escape");
    }
};

}  // namespace

int main() {
    using ttnx::core::SearchErrorCode;
    using ttnx::youtube::GuestSearchFlow;
    using ttnx::youtube::SearchWorkerStage;
    using ttnx::youtube::protect_search_worker_operation;
    using ttnx::youtube::search_worker_stage_label;

    const auto guest = session();

    ThrowingHttpClient bad_alloc_http;
    GuestSearchFlow bad_alloc_flow;
    SearchWorkerStage bad_alloc_stage = SearchWorkerStage::BuildingRequest;
    const auto bad_alloc_result = protect_search_worker_operation(
        bad_alloc_stage,
        [&] {
            bad_alloc_stage = SearchWorkerStage::SendingRequest;
            return bad_alloc_flow.begin(bad_alloc_http, guest, "test");
        },
        [&] { bad_alloc_flow.reset(); });
    expect(!bad_alloc_result.result.page.has_value(), "bad_alloc returns an ordinary failed result");
    expect(bad_alloc_result.result.error_code == SearchErrorCode::MemoryPressure,
           "bad_alloc maps to the bounded memory-pressure error");
    expect(bad_alloc_result.result.error.empty(), "bad_alloc exposes no exception text");
    expect(bad_alloc_result.caught_bad_alloc, "bad_alloc is explicitly identified for safe diagnostics");
    expect(bad_alloc_result.caught_exception, "bad_alloc is contained by the worker boundary");
    expect(bad_alloc_result.failure_stage == SearchWorkerStage::SendingRequest,
           "bad_alloc retains only the coarse safe stage");
    expect(!bad_alloc_flow.active(), "fatal-like first-page failure leaves no partial SearchFlow state");

    ThrowingHttpClient runtime_http;
    runtime_http.failure = ThrowingHttpClient::Failure::Runtime;
    GuestSearchFlow runtime_flow;
    SearchWorkerStage runtime_stage = SearchWorkerStage::BuildingRequest;
    const auto runtime_result = protect_search_worker_operation(
        runtime_stage,
        [&] {
            runtime_stage = SearchWorkerStage::SendingRequest;
            return runtime_flow.begin(runtime_http, guest, "test");
        },
        [&] { runtime_flow.reset(); });
    expect(runtime_result.result.error_code == SearchErrorCode::InternalFailure,
           "std::exception maps to a bounded internal failure");
    expect(runtime_result.result.error.empty(), "std::exception what() text is discarded");
    expect(!runtime_result.caught_bad_alloc, "runtime_error is not misclassified as bad_alloc");
    expect(runtime_result.caught_exception, "runtime_error is contained");
    expect(!runtime_flow.active(), "runtime exception cleanup clears first-page SearchFlow state");

    SearchWorkerStage unknown_stage = SearchWorkerStage::ParsingSearch;
    const auto unknown_result = protect_search_worker_operation(
        unknown_stage,
        [&]() -> ttnx::youtube::GuestSearchResult { throw 7; },
        [] {});
    expect(unknown_result.result.error_code == SearchErrorCode::InternalFailure,
           "non-standard exceptions are also contained");
    expect(unknown_result.caught_exception, "catch-all boundary is active");

    SearchWorkerStage success_stage = SearchWorkerStage::Normalizing;
    const auto success = protect_search_worker_operation(
        success_stage,
        [&] {
            ttnx::youtube::GuestSearchResult result;
            result.error_code = SearchErrorCode::HttpFailure;
            success_stage = SearchWorkerStage::Complete;
            return result;
        },
        [] {});
    expect(success.result.error_code == SearchErrorCode::HttpFailure,
           "successful operation result passes through unchanged");
    expect(!success.caught_exception, "successful operation reports no caught exception");
    expect(success.failure_stage == SearchWorkerStage::Complete,
           "successful operation records its final coarse stage");

    expect(std::string(search_worker_stage_label(SearchWorkerStage::ParsingHttp)) == "response parsing",
           "stage labels expose only bounded safe wording");

    if (failures == 0) {
        std::cout << "All Search worker exception-boundary tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
