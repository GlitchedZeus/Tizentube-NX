from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    s = p.read_text()
    count = s.count(old)
    if count != 1:
        raise SystemExit(f'{path}: expected 1 occurrence, found {count}: {old[:140]!r}')
    p.write_text(s.replace(old, new, 1))


# Harden the Home network boundary so only explicitly recognized Home renderer
# families can be delegated to the shared leaf renderer normalizer. Unknown
# wrappers (including Premium/promo/command-only renderers) stay opaque.
home = Path('src/core/home_response.cpp')
s = home.read_text()
anchor = '''std::optional<std::string_view> browse_tabs(std::string_view response_body) {
'''
helper = r'''bool append_home_entry(
    core::HomePage& home,
    std::unordered_set<std::string>& seen,
    std::string inherited_title,
    std::string_view entry,
    const core::FilterPolicy& policy,
    std::string& continuation,
    bool& ambiguous_continuation,
    std::string& error) {
    // Home is deliberately fail-closed at renderer-family boundaries. Only
    // recognized structural wrappers are opened. Unknown/promo/Premium/command
    // renderers remain opaque even if they contain a video-shaped descendant.
    if (const auto rich_item = field(entry, "richItemRenderer")) {
        const auto content = field(*rich_item, "content");
        return !content || append_home_entry(
            home, seen, std::move(inherited_title), *content, policy,
            continuation, ambiguous_continuation, error);
    }

    if (const auto rich_section = field(entry, "richSectionRenderer")) {
        const auto content = field(*rich_section, "content");
        if (!content) return true;
        const auto title = section_title(entry);
        if (const auto shelf = field(*content, "richShelfRenderer")) {
            const auto contents = field(*shelf, "contents");
            if (!contents) return true;
            const auto items = array_elements(*contents);
            if (!items) {
                error = "Home shelf contents are malformed or exceed limits.";
                return false;
            }
            for (const auto item : *items) {
                if (!append_home_entry(
                        home, seen, title, item, policy,
                        continuation, ambiguous_continuation, error)) {
                    return false;
                }
            }
            return true;
        }
        if (const auto shelf = field(*content, "shelfRenderer")) {
            return append_home_entry(
                home, seen, title, *shelf, policy,
                continuation, ambiguous_continuation, error);
        }
        // reelShelfRenderer and every other unreviewed section type are opaque.
        return true;
    }

    if (const auto item_section = field(entry, "itemSectionRenderer")) {
        const auto contents = field(*item_section, "contents");
        if (!contents) return true;
        const auto items = array_elements(*contents);
        if (!items) {
            error = "Home item-section contents are malformed or exceed limits.";
            return false;
        }
        const auto title = section_title(entry);
        for (const auto item : *items) {
            if (!append_home_entry(
                    home, seen, title, item, policy,
                    continuation, ambiguous_continuation, error)) {
                return false;
            }
        }
        return true;
    }

    if (const auto rich_shelf = field(entry, "richShelfRenderer")) {
        const auto contents = field(*rich_shelf, "contents");
        if (!contents) return true;
        const auto items = array_elements(*contents);
        if (!items) {
            error = "Home rich-shelf contents are malformed or exceed limits.";
            return false;
        }
        const auto title = section_title(entry).empty()
            ? inherited_title
            : section_title(entry);
        for (const auto item : *items) {
            if (!append_home_entry(
                    home, seen, title, item, policy,
                    continuation, ambiguous_continuation, error)) {
                return false;
            }
        }
        return true;
    }

    if (const auto shelf = field(entry, "shelfRenderer")) {
        std::optional<std::string_view> items;
        if (const auto content = field(*shelf, "content")) {
            if (const auto horizontal = field(*content, "horizontalListRenderer")) {
                items = field(*horizontal, "items");
            } else if (const auto expanded = field(*content, "expandedShelfContentsRenderer")) {
                items = field(*expanded, "items");
            }
        }
        if (!items) items = field(*shelf, "contents");
        if (!items) return true;
        const auto children = array_elements(*items);
        if (!children) {
            error = "Home shelf item list is malformed or exceeds limits.";
            return false;
        }
        const auto title = section_title(entry).empty()
            ? inherited_title
            : section_title(entry);
        for (const auto child : *children) {
            if (!append_home_entry(
                    home, seen, title, child, policy,
                    continuation, ambiguous_continuation, error)) {
                return false;
            }
        }
        return true;
    }

    // These are the only leaf renderer families reviewed for the first Home
    // slice. The shared parser still applies the hard Shorts/ad/shopping filter
    // and metadata bounds inside each recognized leaf.
    for (const auto key : {
             std::string_view{"videoRenderer"},
             std::string_view{"channelRenderer"},
             std::string_view{"playlistRenderer"},
             std::string_view{"radioRenderer"},
             std::string_view{"lockupViewModel"},
             std::string_view{"continuationItemRenderer"},
             std::string_view{"continuationItemViewModel"},
             std::string_view{"continuationItemView"}}) {
        if (field(entry, key)) {
            return append_normalized_payload(
                home, seen, std::move(inherited_title), entry, policy,
                continuation, ambiguous_continuation, error);
        }
    }

    return true;
}

std::optional<std::string_view> browse_tabs(std::string_view response_body) {
'''
if s.count(anchor) != 1:
    raise SystemExit('home_response.cpp: browse_tabs anchor mismatch')
s = s.replace(anchor, helper, 1)

old_primary = '''    saw_payload = true;
    for (const auto entry : *entries) {
        if (!append_normalized_payload(
                home,
                seen,
                section_title(entry),
                entry,
                policy,
                continuation,
                ambiguous_continuation,
                error)) {
            return false;
        }
    }
'''
new_primary = '''    saw_payload = true;
    for (const auto entry : *entries) {
        if (!append_home_entry(
                home,
                seen,
                section_title(entry),
                entry,
                policy,
                continuation,
                ambiguous_continuation,
                error)) {
            return false;
        }
    }
'''
if s.count(old_primary) != 1:
    raise SystemExit('home_response.cpp: primary loop mismatch')
s = s.replace(old_primary, new_primary, 1)

old_cont = '''                saw_payload = true;
                if (!append_normalized_payload(
                        home,
                        seen,
                        {},
                        *items,
                        policy,
                        continuation,
                        ambiguous_continuation,
                        error)) {
                    return false;
                }
'''
new_cont = '''                const auto children = array_elements(*items);
                if (!children) {
                    error = "Home continuation items are malformed or exceed limits.";
                    return false;
                }
                saw_payload = true;
                for (const auto child : *children) {
                    if (!append_home_entry(
                            home,
                            seen,
                            {},
                            child,
                            policy,
                            continuation,
                            ambiguous_continuation,
                            error)) {
                        return false;
                    }
                }
'''
if s.count(old_cont) != 1:
    raise SystemExit('home_response.cpp: continuation loop mismatch')
s = s.replace(old_cont, new_cont, 1)
home.write_text(s)

# Explicitly prove unsupported/Premium wrappers cannot leak nested normal videos.
test = Path('tests/home_response_tests.cpp')
ts = test.read_text()
needle = '''                    {"productRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-product",
                      "title": {"simpleText": "Shopping escape attempt"}
                    }}}},
                    {"richSectionRenderer": {"content": {"reelShelfRenderer": {
'''
insert = '''                    {"productRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-product",
                      "title": {"simpleText": "Shopping escape attempt"}
                    }}}},
                    {"mealbarPromoRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-premium-promo",
                      "title": {"simpleText": "Premium escape attempt"}
                    }}}},
                    {"totallyUnknownRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-unknown-wrapper",
                      "title": {"simpleText": "Unsupported escape attempt"}
                    }}}},
                    {"richSectionRenderer": {"content": {"reelShelfRenderer": {
'''
if ts.count(needle) != 1:
    raise SystemExit('home_response_tests.cpp: fixture anchor mismatch')
ts = ts.replace(needle, insert, 1)
needle2 = '''        expect(find_id(page, "home-product") == nullptr,
               "shopping subtree is opaque and contributes zero videos");
        expect(find_id(page, "hostile-topbar-video") == nullptr,
'''
insert2 = '''        expect(find_id(page, "home-product") == nullptr,
               "shopping subtree is opaque and contributes zero videos");
        expect(find_id(page, "home-premium-promo") == nullptr,
               "Premium/promo wrapper is opaque and contributes zero videos");
        expect(find_id(page, "home-unknown-wrapper") == nullptr,
               "unsupported Home renderer family is opaque and contributes zero videos");
        expect(find_id(page, "hostile-topbar-video") == nullptr,
'''
if ts.count(needle2) != 1:
    raise SystemExit('home_response_tests.cpp: assertion anchor mismatch')
ts = ts.replace(needle2, insert2, 1)
test.write_text(ts)

# Settings copy reflects the current M2 surface without touching accepted Search behavior.
replace_once(
    'switch/source/main.cpp',
    'label(content, "TizenTube NX 0.1.0  /  M2 live guest Search", 20);',
    'label(content, "TizenTube NX 0.1.0  /  M2 guest Home + Search", 20);')

# ROADMAP: Search continuation is now physically accepted; Home first-page code
# is implemented but still requires a physical gate.
road = Path('docs/ROADMAP.md')
rs = road.read_text()
rs = rs.replace(
    '- [ ] Real-hardware continuation / `Load more` focus/UI acceptance',
    '- [x] Real-hardware continuation / `Load more` focus/UI acceptance at `428497c90be6e768c607e45c68171827d74f0faf`')
rs = rs.replace(
    '### Remaining guest surfaces\n\n- [ ] Parse/present live Home browse results',
    '### Remaining guest surfaces\n\n- [x] Implement scoped live guest Home first-page request/parser/model/text UI\n- [ ] Real-hardware Home first-page acceptance\n- [ ] Home continuation after first-page acceptance')
old_gate = '''**Guest bootstrap, startup recovery, first-page live Search, inline result detail, and full sidebar traversal are physically accepted. Search continuation data fetch/append has also been proven on hardware; the current gate is continuation focus/scroll acceptance after the `d432ef8...` focus fix. Preserve the accepted non-destructive result View lifecycle, exact `www.youtube.com:443` allowlist, hard Shorts/ad firewall, remote-thumbnail block, and post-v1 OAuth documentation-only boundary during M2.**'''
new_gate = '''**Guest bootstrap, startup recovery, the complete Search slice, inline detail, sidebar traversal, and explicit Search continuation are physically accepted through `428497c90be6e768c607e45c68171827d74f0faf`. The current M2 gate is the first explicit live guest Home page. Preserve the accepted Search implementation, exact `www.youtube.com:443` allowlist, hard Shorts/ad/promoted/shopping firewall, remote-thumbnail block, and post-v1 OAuth documentation-only boundary.**'''
if old_gate not in rs:
    raise SystemExit('ROADMAP current gate text mismatch')
rs = rs.replace(old_gate, new_gate, 1)
road.write_text(rs)

# HARDWARE_GATES: preserve rejected history but close the Search continuation gate
# and define the new Home-only physical acceptance gate.
hw = Path('docs/HARDWARE_GATES.md')
hs = hw.read_text()
accepted_anchor = '''This acceptance closes the base Search UI/navigation lifecycle gate.\n\n## Hardware rejected\n'''
accepted_block = '''This acceptance closes the base Search UI/navigation lifecycle gate.

### Search continuation / Load more — fully accepted

Hardware-tested NRO head: `428497c90be6e768c607e45c68171827d74f0faf`

Physical Switch result: **ACCEPTED**.

This closes the full M2 Search hardware slice. Hardware proved that explicit continuation fetch/append works, the selector transfers to the first newly-loaded result after layout, the viewport follows correctly, Up/Down traversal across the continuation boundary is continuous, inline A/A-again detail toggling still works, and sidebar navigation remains intact. The preceding absolute-pixel `ScrollingFrame` backport was also stress-tested through **156 loaded results** without the earlier disappearing-selector or repeated-pagination exit.

The accepted Search implementation is now a protected baseline for later M2 surfaces.

## Hardware rejected
'''
if accepted_anchor not in hs:
    raise SystemExit('HARDWARE_GATES accepted anchor mismatch')
hs = hs.replace(accepted_anchor, accepted_block, 1)
old_current_start = '## Current physical gate — Search continuation post-load focus handoff\n'
idx = hs.find(old_current_start)
if idx < 0:
    raise SystemExit('HARDWARE_GATES current gate heading missing')
inv = hs.find('## Invariants for every gate\n', idx)
if inv < 0:
    raise SystemExit('HARDWARE_GATES invariants heading missing')
home_gate = '''## Current physical gate — guest Home first page

Search continuation is fully hardware accepted at `428497c90be6e768c607e45c68171827d74f0faf`. The next gate is deliberately narrower and must not reopen accepted Search behavior.

The Home candidate must prove on real hardware that:

- startup still performs no hidden YouTube Home request;
- after the explicit guest bootstrap reaches `Guest ready`, `Load Home` performs the first `FEwhat_to_watch` browse request;
- normal Video / Channel / Playlist results render as text-only controller-focusable rows where YouTube provides them;
- no Shorts/reels, ads/promoted content, shopping/product content, Premium/promo wrappers or unsupported renderer descendants reach the Home model;
- A toggles normalized stable ID + clean canonical URL inline without deleting the focused row;
- B returns to the Home SidebarItem and all five sidebar sections remain reachable;
- leaving and returning to Home safely reconstructs from `HomeModel` without stale Borealis View pointers;
- the accepted Search first page + Load more focus handoff still work afterward;
- remote thumbnails remain disabled and Home continuation remains deferred until this first-page gate passes.

Home hardware acceptance remains **NO** until this sequence passes on the physical Switch.

'''
hs = hs[:idx] + home_gate + hs[inv:]
hw.write_text(hs)

# PROJECT_STATUS: update the executive status and current activation gate, then add
# a concise implementation section before Search hardening.
ps = Path('docs/PROJECT_STATUS.md')
ss = ps.read_text()
old_intro = '''The real-Switch guest bootstrap, startup-recovery, first-page live Search, inline result-detail, and full sidebar traversal gates are physically accepted. Hardware-tested NRO head `d6910a232732b2bd9169abb11dfdf320cf35a34b` closes the blank-Search/Home-Search-focus regression introduced by `36015cb...`. Physical testing of continuation build `959a46e7c1d5de1743cdf3b11a48b1b227ab0b4e` proved that `Load more` fetches and appends additional videos, but rejected that overall UI checkpoint because the selector could move out of view and Up from `Load more` could jump to result 1. Focus-lifecycle fix `d432ef8c6eee8e1b77ecd8a96b78832c43bb8a5b` now explicitly routes Load-more-Up to the last result, defers focus to the first newly appended result after layout, and adds A-again inline-detail collapse; physical retest remains required.'''
new_intro = '''The real-Switch guest bootstrap, startup recovery, and complete M2 Search slice are physically accepted. Hardware-tested Search head `428497c90be6e768c607e45c68171827d74f0faf` closes the continuation focus gate: explicit Load more appends successfully, focus lands on the first newly-loaded result, the viewport follows, A-again collapses inline detail, sidebar navigation remains intact, and the underlying pixel-scroll foundation was previously stress-tested through 156 loaded results. Development has now moved to the first live guest Home page. Home is implemented as an explicit post-Guest-ready action with a scoped Home parser/model and text-only UI; it remains pending physical Switch acceptance.'''
if old_intro not in ss:
    raise SystemExit('PROJECT_STATUS intro mismatch')
ss = ss.replace(old_intro, new_intro, 1)

hardening_anchor = '\n## Search hardening state\n'
home_section = '''
## Live guest Home first-page implementation

The first Home slice reuses the accepted app-lifetime `LibnxHttpClient`, memory-only guest session and exact `www.youtube.com:443` network policy. Startup still performs no hidden Home request. After the user explicitly reaches `Guest ready`, Home exposes `Load Home` / `Refresh Home`, which sends the existing minimal `FEwhat_to_watch` browse request off the Borealis/UI thread.

The network-facing parser is scoped to the selected Home browse tab and recognized continuation action arrays. It does not recursively scan topbar/menu/command siblings. Within the selected Home container, only reviewed structural wrappers and leaf renderer families are opened; unreviewed, Premium/promo and command-only wrappers remain opaque even if they contain a video-shaped descendant. Recognized leaf renderers reuse the bounded Video / Channel / Playlist normalization and hard Shorts/reel, ads/promoted and shopping firewall already exercised by Search.

`HomeModel` owns normalized data, generation and selected identity independently from Borealis Views. TabFrame can therefore destroy and reconstruct the Home page without giving a worker stale UI pointers. The first Switch presentation is intentionally text-only: normalized thumbnail candidates may exist in memory but no remote image host is contacted. A toggles the same clean canonical ID/URL detail style proven by Search. Home continuation tokens are parsed as an architectural seam but the UI intentionally defers Home pagination until the first page passes hardware.

Home is **IMPLEMENTED / HOST-VALIDATED / PENDING PHYSICAL SWITCH TEST**. It is not yet called hardware accepted.
'''
if hardening_anchor not in ss:
    raise SystemExit('PROJECT_STATUS Search hardening anchor missing')
ss = ss.replace(hardening_anchor, home_section + hardening_anchor, 1)

old_gate = '''The next activation gate is the explicit continuation / `Load more` candidate at `534f576a77aeb8f7226b2f3a3549be23c621e06a`. It is implemented and host validated but is **NOT physically accepted** until the real Switch proves append-only pagination, continuation-added selection, sidebar stability, new-query invalidation and normal exit/relaunch. Startup boot markers, remote-thumbnail blocking, and the exact `www.youtube.com:443` allowlist remain unchanged.'''
new_gate = '''Search continuation / `Load more` is now **PHYSICALLY ACCEPTED** at `428497c90be6e768c607e45c68171827d74f0faf`, completing the Search hardware slice. The next activation gate is the explicit first-page guest Home candidate. It is implemented and host validated but is **NOT physically accepted** until the real Switch proves normal Home results, inline A/A-again detail, Home tab lifetime/sidebar stability, Search non-regression and normal exit/relaunch. Home continuation and remote thumbnails remain disabled for this gate; the exact `www.youtube.com:443` allowlist remains unchanged.'''
if old_gate not in ss:
    raise SystemExit('PROJECT_STATUS activation gate mismatch')
ss = ss.replace(old_gate, new_gate, 1)
ps.write_text(ss)
