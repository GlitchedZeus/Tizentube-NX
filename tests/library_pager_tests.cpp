#include "tizentube_nx/ui/library_pager.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace ttnx::ui;
int main(){
    LibraryPager none;assert(none.pages()==0 && none.range().first==0);
    assert(!none.next()&&!none.previous());
    LibraryPager one(1);assert(one.pages()==1);
    assert(one.range().second==1 && !one.next());
    LibraryPager small(20);assert(small.pages()==1&&!small.next());
    LibraryPager twenty_one(21);assert(twenty_one.pages()==2);
    assert(twenty_one.next());assert(twenty_one.range().first==20);
    assert(twenty_one.range().second==21&&!twenty_one.next());
    assert(twenty_one.previous() && twenty_one.range().first==0);
    LibraryPager big(1000);
    std::size_t traversed=0,views_per_page=0;
    do {const auto [first,last]=big.range();traversed+=last-first;
        views_per_page=std::max(views_per_page,last-first);
    }while(big.next());
    assert(traversed==1000 && views_per_page==20);
    assert(big.page()==49 && !big.next());
    assert(big.previous() && big.page()==48);
    return 0;
}
