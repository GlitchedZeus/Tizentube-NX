#pragma once

#include "tizentube_nx/core/search_error.hpp"
#include "tizentube_nx/youtube/guest_search.hpp"

#include <exception>
#include <new>
#include <utility>

namespace ttnx::youtube {

enum class SearchWorkerStage {
    Idle,
    BuildingRequest,
    SendingRequest,
    ReceivingResponse,
    ParsingHttp,
    ParsingSearch,
    Normalizing,
    PublishingResult,
    RenderingResults,
    Complete,
};

[[nodiscard]] constexpr const char* search_worker_stage_label(SearchWorkerStage stage) noexcept {
    switch (stage) {
        case SearchWorkerStage::Idle: return "idle";
        case SearchWorkerStage::BuildingRequest: return "building request";
        case SearchWorkerStage::SendingRequest: return "sending request";
        case SearchWorkerStage::ReceivingResponse: return "receiving response";
        case SearchWorkerStage::ParsingHttp: return "response parsing";
        case SearchWorkerStage::ParsingSearch: return "search parsing";
        case SearchWorkerStage::Normalizing: return "normalizing results";
        case SearchWorkerStage::PublishingResult: return "publishing result";
        case SearchWorkerStage::RenderingResults: return "rendering results";
        case SearchWorkerStage::Complete: return "complete";
    }
    return "search worker";
}

struct ProtectedSearchOperationResult {
    GuestSearchResult result;
    SearchWorkerStage failure_stage{SearchWorkerStage::Idle};
    bool caught_bad_alloc{false};
    bool caught_exception{false};
};

// Absolute exception boundary for a Search worker operation. The operation and
// cleanup callbacks may throw, but no exception is allowed to escape this
// function. Exception text is deliberately discarded so arbitrary response or
// library details can never leak into UI/log diagnostics.
template <typename Operation, typename Cleanup>
[[nodiscard]] ProtectedSearchOperationResult protect_search_worker_operation(
    SearchWorkerStage& stage,
    Operation&& operation,
    Cleanup&& cleanup) noexcept {
    try {
        ProtectedSearchOperationResult protected_result;
        protected_result.result = std::forward<Operation>(operation)();
        protected_result.failure_stage = stage;
        return protected_result;
    } catch (const std::bad_alloc&) {
        try {
            std::forward<Cleanup>(cleanup)();
        } catch (...) {
        }
        ProtectedSearchOperationResult protected_result;
        protected_result.result.error_code = core::SearchErrorCode::MemoryPressure;
        protected_result.failure_stage = stage;
        protected_result.caught_bad_alloc = true;
        protected_result.caught_exception = true;
        return protected_result;
    } catch (const std::exception&) {
        try {
            std::forward<Cleanup>(cleanup)();
        } catch (...) {
        }
        ProtectedSearchOperationResult protected_result;
        protected_result.result.error_code = core::SearchErrorCode::InternalFailure;
        protected_result.failure_stage = stage;
        protected_result.caught_exception = true;
        return protected_result;
    } catch (...) {
        try {
            std::forward<Cleanup>(cleanup)();
        } catch (...) {
        }
        ProtectedSearchOperationResult protected_result;
        protected_result.result.error_code = core::SearchErrorCode::InternalFailure;
        protected_result.failure_stage = stage;
        protected_result.caught_exception = true;
        return protected_result;
    }
}

}  // namespace ttnx::youtube
