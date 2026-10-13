#pragma once
#include <algorithm>
#include <cstddef>
#include <utility>

namespace ttnx::ui {
// Replaces (not appends) a constant number of Borealis controls per page.
// Independent of the local Library store and of the Google API.
class LibraryPager final {
public:
    static constexpr std::size_t kPageSize=20;
    explicit constexpr LibraryPager(std::size_t records=0) noexcept
        :records_(records){}
    [[nodiscard]] constexpr std::size_t count()const noexcept {return records_;}
    [[nodiscard]] constexpr std::size_t pages()const noexcept {
        return records_ / kPageSize + (records_ % kPageSize ? 1 : 0);
    }
    [[nodiscard]] constexpr std::size_t page()const noexcept {return page_;}
    [[nodiscard]] constexpr std::pair<std::size_t,std::size_t> range()const noexcept {
        const auto start=page_*kPageSize;
        return {start,std::min(records_,start+kPageSize)};
    }
    [[nodiscard]] constexpr bool has_previous()const noexcept {return page_>0;}
    [[nodiscard]] constexpr bool has_next()const noexcept {return page_+1<pages();}
    constexpr bool previous()noexcept {if(!has_previous())return false;--page_;return true;}
    constexpr bool next()noexcept {if(!has_next())return false;++page_;return true;}
private:
    std::size_t records_{0};
    std::size_t page_{0};
};
static_assert(LibraryPager{}.pages()==0);
static_assert(LibraryPager{1000}.pages()==50);
} // namespace ttnx::ui
