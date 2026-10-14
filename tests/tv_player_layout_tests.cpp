#include "tizentube_nx/ui/tv_player_layout.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <string_view>
using namespace ttnx::ui;
int main() {
    const auto eq=[](float a,float b){return std::fabs(a-b)<0.001f;};
    const auto full=TvPlayerLayout::media_view(TvPlayerLayout::Mode::Fullscreen);
    const auto inset=TvPlayerLayout::media_view(TvPlayerLayout::Mode::Settings);
    assert(eq(full.w,1280) && eq(full.h,720));
    assert(eq(inset.x,36) && eq(inset.w,764) && eq(inset.h,429.75f));
    assert(eq(inset.y,145.125f) && eq(inset.bottom(),574.875f));
    assert(inset.right()<TvPlayerLayout::kPanelX);
    assert(eq(TvPlayerLayout::media_view(TvPlayerLayout::Mode::Settings,1.5f).w,1146));
    assert(eq(TvPlayerLayout::media_view(TvPlayerLayout::Mode::Fullscreen,0).w,0));
    const auto seek=TvPlayerLayout::seek_hitbox();
    assert(eq(seek.x,64) && eq(seek.w,1152));
    assert(eq(seek.y,459) && eq(seek.bottom(),489));
    const auto panel=TvPlayerLayout::panel_row(0);
    assert(eq(panel.x,836) && eq(panel.y,80));
    assert(eq(TvPlayerLayout::panel_row(1).y,152));
    assert(TvPlayerLayout::panel_row(7).bottom()<=720);
    const auto card=TvPlayerLayout::related_card(0);
    assert(eq(card.x,64) && eq(card.y,598));
    assert(eq(card.w*9,card.h*16));
    assert(eq(TvPlayerLayout::related_card(1).x,456));
    assert(eq(TvPlayerLayout::related_card(2,120).x,728));
    assert(eq(TvPlayerLayout::clamped_seek_fraction(35,100),0.35f));
    assert(eq(TvPlayerLayout::clamped_seek_fraction(300,100),1));
    assert(eq(TvPlayerLayout::clamped_seek_fraction(-5,100),0));
    assert(eq(TvPlayerLayout::clamped_seek_fraction(0,0),0));
    assert(eq(TvPlayerLayout::clamped_seek_fraction(5,-10),0));
    for(const auto height:TvPlayerLayout::kCandidateQualityHeights)
        assert(height<=720 && height>=240);
    for(const auto item:TvPlayerLayout::kCandidateSettings) {
        assert(item!="Google account" && item!="Purchases" && item!="Shorts");
    }
    return 0;
}
