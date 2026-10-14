#include "tizentube_nx/ui/tv_player_controls.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
using namespace ttnx::ui;
int main() {
    TvPlayerControls ui;
    assert(ui.transport_visible());
    ui.activate(100);
    assert(ui.panel()==TvPlayerControls::Panel::Transport);
    ui.tick(4899,false);
    assert(ui.transport_visible());
    ui.tick(4900,false);
    assert(!ui.transport_visible());
    assert(!ui.close_one(4901));
    ui.toggle(4902);
    assert(ui.transport_visible());
    ui.diagnostics(5000);
    ui.tick(20000,false);
    assert(ui.panel()==TvPlayerControls::Panel::Diagnostics);
    assert(ui.close_one(20001) && ui.panel()==TvPlayerControls::Panel::Transport);
    assert(ui.close_one(20002) && !ui.transport_visible());
    ui.activate(25000);
    ui.tick(100000,true);
    assert(ui.transport_visible());
    ui.activity(101000);
    ui.tick(105800,false);
    assert(!ui.transport_visible());
    assert(TvPlayerControls::progress(25,100)==0.25f);
    assert(TvPlayerControls::progress(110,100)==1.0f);
    assert(TvPlayerControls::progress(-7,100)==0.0f);
    assert(TvPlayerControls::progress(5,0)==0.0f);
    assert(TvPlayerControls::progress(std::numeric_limits<double>::quiet_NaN(),100)==0.0f);
    assert(TvPlayerControls::progress(10,std::numeric_limits<double>::infinity())==0.0f);
    assert(TvPlayerControls::bounded_seek(5,-10,120)==0);
    assert(TvPlayerControls::bounded_seek(120,10,120)==120);
    assert(TvPlayerControls::bounded_seek(10,15,120)==25);
    assert(TvPlayerControls::bounded_seek(5,-2,-1)==3);
    return 0;
}
