// Local compatibility backport for the pinned natinusala/borealis ScrollingFrame.
//
// The pinned 2021 implementation stores scroll position as a fraction of the
// current content height. That representation becomes inconsistent when a
// focused list grows dynamically (for example when Search appends a YouTube
// continuation page): the fraction is retained while the content height changes,
// but the already-applied pixel translation is not. Controller focus and the
// visible viewport can then diverge.
//
// Mature Switch Borealis forks (including StreamFin's) moved scrolling to an
// absolute pixel content offset. Keep the pinned public API/ABI, but backport
// that stable representation here. Makefile.switch excludes the submodule's
// scrolling_frame.cpp so these definitions are the only ones linked.

#include <algorithm>
#include <cmath>

#include <borealis/core/application.hpp>
#include <borealis/core/util.hpp>
#include <borealis/views/scrolling_frame.hpp>

namespace brls
{

ScrollingFrame::ScrollingFrame()
{
    BRLS_REGISTER_ENUM_XML_ATTRIBUTE(
        "scrollingBehavior", ScrollingBehavior, this->setScrollingBehavior,
        {
            { "natural", ScrollingBehavior::NATURAL },
            { "centered", ScrollingBehavior::CENTERED },
        });

    this->setMaximumAllowedXMLElements(1);
}

void ScrollingFrame::draw(
    NVGcontext* vg,
    float x,
    float y,
    float width,
    float height,
    Style style,
    FrameContext* ctx)
{
    // Update scrolling - try until it works. This is also used after a view
    // first appears and is intentionally kept compatible with pinned Borealis.
    if (this->updateScrollingOnNextFrame && this->updateScrolling(false))
        this->updateScrollingOnNextFrame = false;

    nvgSave(vg);
    float scrollingTop    = this->getScrollingAreaTopBoundary();
    float scrollingHeight = this->getScrollingAreaHeight();
    nvgIntersectScissor(vg, x, scrollingTop, this->getWidth(), scrollingHeight);

    Box::draw(vg, x, y, width, height, style, ctx);

    nvgRestore(vg);
}

void ScrollingFrame::addView(View* view)
{
    this->setContentView(view);
}

void ScrollingFrame::removeView(View* view)
{
    this->setContentView(nullptr);
}

void ScrollingFrame::setContentView(View* view)
{
    if (this->contentView)
    {
        Box::removeView(this->contentView);
        this->contentView = nullptr;
    }

    if (!view)
        return;

    this->contentView = view;

    view->detach();
    view->setCulled(false);
    view->setMaxWidth(this->getWidth());
    view->setDetachedPosition(this->getX(), this->getY());

    Box::addView(view);
}

void ScrollingFrame::onLayout()
{
    if (this->contentView)
    {
        this->contentView->setMaxWidth(this->getWidth());
        this->contentView->setDetachedPosition(this->getX(), this->getY());
        this->contentView->invalidate();
    }
}

float ScrollingFrame::getScrollingAreaTopBoundary()
{
    return this->getY();
}

float ScrollingFrame::getScrollingAreaHeight()
{
    return this->getHeight();
}

void ScrollingFrame::willAppear(bool resetState)
{
    this->prebakeScrolling();

    if (resetState)
    {
        this->startScrolling(false, 0.0f);
        this->updateScrollingOnNextFrame = true;
    }

    Box::willAppear(resetState);
}

void ScrollingFrame::prebakeScrolling()
{
    float y      = this->getScrollingAreaTopBoundary();
    float height = this->getScrollingAreaHeight();

    this->middleY = y + height / 2;
    this->bottomY = y + height;
}

void ScrollingFrame::startScrolling(bool animated, float newScroll)
{
    const float contentHeight = this->getContentHeight();
    const float maxScroll = std::max(0.0f, contentHeight - this->getScrollingAreaHeight());
    newScroll = std::clamp(newScroll, 0.0f, maxScroll);

    const float currentScroll = static_cast<float>(this->scrollY);
    if (std::fabs(newScroll - currentScroll) < 0.01f)
    {
        // Re-apply the pixel translation even when the logical offset did not
        // change. This matters after a detached Yoga tree was relaid out.
        this->scrollAnimationTick();
        return;
    }

    this->scrollY.stop();

    if (animated)
    {
        Style style = Application::getStyle();

        this->scrollY.reset();
        this->scrollY.addStep(
            newScroll,
            style["brls/animations/highlight"],
            EasingFunction::quadraticOut);
        this->scrollY.setTickCallback([this] {
            this->scrollAnimationTick();
        });
        this->scrollY.start();
    }
    else
    {
        this->scrollY = newScroll;
        this->scrollAnimationTick();
    }

    this->invalidate();
}

void ScrollingFrame::setScrollingBehavior(ScrollingBehavior behavior)
{
    this->behavior = behavior;
}

float ScrollingFrame::getContentHeight()
{
    if (!this->contentView)
        return 0;

    return this->contentView->getHeight();
}

void ScrollingFrame::scrollAnimationTick()
{
    if (!this->contentView)
        return;

    const float contentHeight = this->getContentHeight();
    const float maxScroll = std::max(0.0f, contentHeight - this->getScrollingAreaHeight());
    float offset = std::clamp(static_cast<float>(this->scrollY), 0.0f, maxScroll);

    if (std::fabs(offset - static_cast<float>(this->scrollY)) >= 0.01f)
        this->scrollY = offset;

    this->contentView->setTranslationY(-offset);
}

void ScrollingFrame::onChildFocusGained(View* directChild, View* focusedView)
{
    this->childFocused = true;

    this->updateScrolling(true);

    Box::onChildFocusGained(directChild, focusedView);
}

void ScrollingFrame::onChildFocusLost(View* directChild, View* focusedView)
{
    this->childFocused = false;
}

bool ScrollingFrame::updateScrolling(bool animated)
{
    if (!this->contentView || !this->childFocused)
        return false;

    View* focusedView = Application::getCurrentFocus();
    if (!focusedView)
        return false;

    this->prebakeScrolling();

    const float contentHeight = this->getContentHeight();
    if (contentHeight <= 0.0f)
        return false;

    const float currentSelectionMiddleOnScreen =
        focusedView->getY() + focusedView->getHeight() / 2.0f;

    // scrollY is intentionally treated as an ABSOLUTE positive pixel offset,
    // not the pinned implementation's 0..1 percentage. Since getY() already
    // includes the detached content translation, adding the screen-space delta
    // keeps the focused control centered even after content height changes.
    float newScroll = static_cast<float>(this->scrollY) +
                      (currentSelectionMiddleOnScreen - this->middleY);

    const float maxScroll = std::max(0.0f, contentHeight - this->getScrollingAreaHeight());
    newScroll = std::clamp(newScroll, 0.0f, maxScroll);

    this->startScrolling(animated, newScroll);
    return true;
}

#define NO_PADDING fatal("Padding is not supported by brls:ScrollingFrame, please set padding on the content view instead");

void ScrollingFrame::setPadding(float top, float right, float bottom, float left)
{
    NO_PADDING
}

void ScrollingFrame::setPaddingTop(float top)
{
    NO_PADDING
}

void ScrollingFrame::setPaddingRight(float right)
{
    NO_PADDING
}

void ScrollingFrame::setPaddingBottom(float bottom)
{
    NO_PADDING
}

void ScrollingFrame::setPaddingLeft(float left)
{
    NO_PADDING
}

View* ScrollingFrame::create()
{
    return new ScrollingFrame();
}

} // namespace brls
