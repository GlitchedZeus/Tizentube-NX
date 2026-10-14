#include "tizentube_nx/ui/tv_layout.hpp"
#include "tizentube_nx/ui/navigation.hpp"
#ifdef NDEBUG
#undef NDEBUG // keep runtime assertions active in Release CTest runs
#endif
#include <cassert>
#include <cmath>
using namespace ttnx::ui;
int main() {
    auto equal=[](float x,float y) {return std::fabs(x-y)<0.001f;};
    assert(equal(TvLayout::scale(1280,720),1));
    assert(equal(TvLayout::scale(1920,1080),1.5f));
    assert(equal(TvLayout::scale(800,720),0.625f));
    assert(equal(TvLayout::scale(0,720),0));
    const auto collapsed=TvLayout::card(0,TvRailMode::Collapsed);
    const auto expanded=TvLayout::card(0,TvRailMode::Expanded);
    assert(equal(collapsed.x,104) && equal(expanded.x,356));
    assert(equal(expanded.y,102) && equal(expanded.w,352) && equal(expanded.h,198));
    assert(equal(TvLayout::card(1,TvRailMode::Expanded).x,732));
    assert(equal(TvLayout::card(2,TvRailMode::Expanded).x,1108));
    assert(equal(TvLayout::album(1,TvRailMode::Collapsed).x,328));
    assert(equal(TvLayout::card(0,TvRailMode::Expanded,0,1.5f).w,528));
    assert(equal(TvLayout::scroll_to_reveal(0,5,TvRailMode::Expanded,0),0));
    const float into_view=TvLayout::scroll_to_reveal(2,5,TvRailMode::Expanded,0);
    assert(equal(into_view,196));
    assert(equal(TvLayout::scroll_to_reveal(2,5,TvRailMode::Expanded,into_view),into_view));
    assert(equal(TvLayout::scroll_to_reveal(0,5,TvRailMode::Expanded,into_view),0));
    assert(equal(TvLayout::scroll_to_reveal(9,2,TvRailMode::Expanded,30),0));
    assert(equal(TvLayout::scroll_to_reveal(0,0,TvRailMode::Collapsed,30),0));
    assert(equal(TvLayout::scroll_to_reveal(1,2,TvRailMode::Collapsed,0),0));
    assert(kRootNavigation.size()==5);
    assert(!root_navigation_contains("Shorts"));
    assert(!root_navigation_contains("Sign in"));
    assert(!root_navigation_contains("Purchases"));
    return 0;
}
