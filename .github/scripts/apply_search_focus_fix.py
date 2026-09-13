from pathlib import Path
import re


def replace_once(text, old, new, name):
    count = text.count(old)
    assert count == 1, f"{name}: expected 1 match, found {count}"
    return text.replace(old, new, 1)


p = Path('switch/source/main.cpp')
s = p.read_text()

# Keep the current result buttons alive when selecting a result. The rejected
# 36015cb hardware build deleted the focused row, rebuilt the whole list, and
# force-focused a replacement. On pinned Borealis, Search page construction
# also happens inside a SidebarItem activation callback before TabFrame attaches
# the returned page, so forced focus there steals controller focus from sidebar.
s = replace_once(
    s,
    '#include <thread>\n#include <utility>\n',
    '#include <thread>\n#include <utility>\n#include <vector>\n',
    'vector include')

old_tick = '''    void tick() {
        pump_network_result();
        pump_search_result();
        if (search_selection_rebuild_pending_) {
            // The A-button callback has returned before tick() runs.
            // Rebuild here instead of deleting the focused result
            // button while its own click callback is executing.
            search_selection_rebuild_pending_ = false;
            rebuild_search_results();
        }
        update_frame_rate();
    }'''
new_tick = '''    void tick() {
        pump_network_result();
        pump_search_result();
        update_frame_rate();
    }'''
s = replace_once(s, old_tick, new_tick, 'remove deferred destructive rebuild')

old_members = '''    brls::Button* search_action_button_{nullptr};
    brls::Box* search_results_box_{nullptr};
    bool search_selection_rebuild_pending_{false};'''
new_members = '''    brls::Button* search_action_button_{nullptr};
    brls::Box* search_results_box_{nullptr};

    struct SearchDetailRow {
        std::string identity;
        brls::Label* detail{nullptr};
    };
    std::vector<SearchDetailRow> search_detail_rows_;'''
s = replace_once(s, old_members, new_members, 'detail row state')

pattern = re.compile(
    r'    void rebuild_search_results\(\) \{.*?\n    \}\n\n    void refresh_search_page\(bool rebuild_results\)',
    re.S)
replacement = '''    void refresh_search_inline_details() {
        const auto& selected_identity = search_model_.selected_identity();
        for (auto& row : search_detail_rows_) {
            if (!row.detail) continue;
            const bool selected = !selected_identity.empty() && row.identity == selected_identity;
            row.detail->setVisibility(
                selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
    }

    void rebuild_search_results() {
        if (!search_results_box_) return;

        // Full rebuilding is reserved for a genuinely new/reconstructed result
        // page. Selecting a result never comes through this path, so the focused
        // Borealis button remains alive for the entire A-button interaction.
        search_detail_rows_.clear();
        while (!search_results_box_->getChildren().empty()) {
            search_results_box_->removeView(search_results_box_->getChildren().back());
        }

        const auto selected_identity = search_model_.selected_identity();

        for (const auto& result : search_model_.results()) {
            const auto identity = ttnx::core::browse_result_identity(result);
            if (identity.empty()) continue;

            std::string title = "[" + ttnx::core::search_result_kind_label(result.kind) + "] ";
            title += result.title.empty() ? "Untitled result" : result.title;
            auto* select = button(
                search_results_box_,
                bounded_ui_text(title, kMaxUiTitleChars));
            select->setHeight(72);
            select->registerClickAction([this, identity](brls::View*) {
                if (!search_model_.select(identity)) return true;
                // Non-destructive selection update: keep this focused button and
                // the surrounding result tree alive. Only detail visibility changes.
                refresh_search_inline_details();
                return true;
            });

            auto* detail_label = label(
                search_results_box_,
                ttnx::core::search_result_selection_display(result),
                18);
            detail_label->setMarginBottom(14);
            detail_label->setVisibility(
                identity == selected_identity
                    ? brls::Visibility::VISIBLE
                    : brls::Visibility::GONE);
            search_detail_rows_.push_back({identity, detail_label});

            const auto metadata = ttnx::core::search_result_metadata_display(result);
            if (!metadata.empty()) {
                auto* metadata_label = label(
                    search_results_box_,
                    bounded_ui_text(metadata, kMaxUiMetadataChars),
                    18);
                metadata_label->setMarginBottom(14);
            }
        }
    }

    void refresh_search_page(bool rebuild_results)'''
s, n = pattern.subn(replacement, s, count=1)
assert n == 1, f'replace rebuild_search_results: expected 1 match, found {n}'

# Page-local detail pointers must never survive a TabFrame page rebuild.
s = replace_once(
    s,
    '        search_action_button_ = nullptr;\n        search_results_box_ = nullptr;\n        auto* scroll = new brls::ScrollingFrame();',
    '        search_action_button_ = nullptr;\n        search_results_box_ = nullptr;\n        search_detail_rows_.clear();\n        auto* scroll = new brls::ScrollingFrame();',
    'clear detail row pointers')

# Borealis TabFrame switches tabs when a SidebarItem becomes active. Focusing
# the Sidebar container resolves via getDefaultFocus() to Home, so B from Search
# accidentally changed tabs and deleted Search. Resolve the active section's
# actual SidebarItem instead. Refocusing an already-active item does not fire
# TabFrame's active-event callback and therefore does not destroy the page.
marker = '    brls::View* create_page(ttnx::ui::RootSection section) {'
assert s.count(marker) == 1
helper = '''    brls::View* sidebar_item_for_section(ttnx::ui::RootSection section) {
        auto* sidebar = dynamic_cast<brls::Box*>(getView("brls/tab_frame/sidebar"));
        if (!sidebar || sidebar->getChildren().empty()) return nullptr;

        auto* item_box = dynamic_cast<brls::Box*>(sidebar->getChildren().front());
        if (!item_box) return nullptr;

        std::size_t wanted = ttnx::ui::kRootNavigation.size();
        for (std::size_t i = 0; i < ttnx::ui::kRootNavigation.size(); ++i) {
            if (ttnx::ui::kRootNavigation[i].section == section) {
                wanted = i;
                break;
            }
        }

        auto& items = item_box->getChildren();
        if (wanted >= items.size()) return nullptr;
        return items[wanted];
    }

    void focus_sidebar_section(ttnx::ui::RootSection section) {
        if (auto* item = sidebar_item_for_section(section)) {
            brls::Application::giveFocus(item);
            return;
        }

        // Conservative fallback for an unexpected pinned-framework layout mismatch.
        if (auto* sidebar = getView("brls/tab_frame/sidebar")) {
            brls::Application::giveFocus(sidebar);
        }
    }

'''
s = s.replace(marker, helper + marker, 1)

old_sections = '''        content->registerAction("Sections", brls::BUTTON_B, [this](brls::View*) {
            brls::Application::giveFocus(getView("brls/tab_frame/sidebar"));
            return true;
        });'''
new_sections = '''        content->registerAction("Sections", brls::BUTTON_B, [this, section](brls::View*) {
            focus_sidebar_section(section);
            return true;
        });'''
s = replace_once(s, old_sections, new_sections, 'section B focus target')

old_explore = '''            button(content, "Explore the sections")->registerClickAction([this](brls::View*) {
                brls::Application::giveFocus(getView("brls/tab_frame/sidebar"));
                return true;
            });'''
new_explore = '''            button(content, "Explore the sections")->registerClickAction([this](brls::View*) {
                focus_sidebar_section(ttnx::ui::RootSection::Home);
                return true;
            });'''
s = replace_once(s, old_explore, new_explore, 'home sidebar focus target')

assert 'search_selection_rebuild_pending_' not in s
assert 'if (selected_button) brls::Application::giveFocus(selected_button);' not in s
assert 'refresh_search_inline_details();' in s
assert 'focus_sidebar_section(section);' in s
p.write_text(s)

# Hardware-gate documentation: 36015cb proved inline detail rendering but
# rejected the overall UI because Search blanked and sidebar focus became trapped.
p = Path('docs/HARDWARE_GATES.md')
s = p.read_text()
marker = '## Current physical gate\n'
assert marker in s
rejected = '''### Inline detail with destructive result rebuild — rejected overall

Checkpoint: `36015cb2bf9ae98fc983674563e9df0935232123`

Hardware proved that inline result detail itself works: A stays on Search, normalized ID and clean canonical URL render inline for upper/middle/lower results, and result-list controller focus remains usable.

The overall UI checkpoint is nevertheless **REJECTED**. After Search selection and return toward the sidebar, Search can become blank and sidebar traversal becomes trapped between Home and Search; Subscriptions, Library and Settings remain drawn but unreachable.

Pinned Borealis explains the two unsafe interactions removed by the next candidate:

- the inline implementation deleted/recreated the entire result subtree, including the focused result, then force-focused a newly-created result;
- B focused the Sidebar container rather than the active section item. `Application::giveFocus()` resolves the container to its default descendant (Home), so TabFrame synchronously removes the Search page. Re-entering Search rebuilt the selected result during the Sidebar focus callback and force-focused that unattached result, stealing focus away from the sidebar before traversal could continue below Search.

The replacement keeps result buttons alive and toggles pre-created detail labels non-destructively. B resolves the active section's actual SidebarItem, so returning to the sidebar no longer changes tabs or destroys the current Search page.

'''
s = s.replace(marker, rejected + marker, 1)

current_pat = re.compile(r'## Current physical gate\n.*?\n## Invariants for every gate', re.S)
current = '''## Current physical gate

The next candidate fixes the rejected `36015cb...` focus/page-lifecycle behavior while preserving all accepted networking and first-page Search code.

Required physical acceptance:

- selecting top/middle/bottom results shows only that row's inline ID/clean URL without rebuilding the result tree;
- the focused result button remains the same live Borealis View;
- B keeps Search populated and focuses the Search sidebar item;
- Up reaches Home and Down traverses Search -> Subscriptions -> Library -> Settings;
- opening each non-Search section and returning to Search reconstructs current SearchModel results safely;
- a second Search still works;
- normal exit/relaunch still works.

`Load more` remains intentionally disabled until this full UI/navigation gate passes.

## Invariants for every gate'''
s, n = current_pat.subn(current, s, count=1)
assert n == 1, f'hardware current gate replacement: {n}'
p.write_text(s)

p = Path('docs/PROJECT_STATUS.md')
s = p.read_text()
old = 'The real-Switch guest bootstrap, startup-recovery, and first-page live Search gates are accepted. The accepted first-page Search checkpoint is `a5fb638abd6e41d82490a460f41ea096b11d1ea5`; the next UI-only checkpoint fixes result-detail visibility without changing Search networking.'
new = 'The real-Switch guest bootstrap, startup-recovery, and first-page live Search gates remain accepted. Checkpoint `36015cb2bf9ae98fc983674563e9df0935232123` proved inline result detail rendering but is rejected as an overall UI checkpoint because Search can blank and sidebar traversal becomes trapped at Home/Search. The current UI-only fix removes destructive selection-time result rebuilding and returns B to the active sidebar item without changing Search networking.'
s = replace_once(s, old, new, 'project current phase')
addition = '''
## Inline-detail focus/page-lifecycle regression — 2026-09-13

Physical hardware rejected `36015cb2bf9ae98fc983674563e9df0935232123` as an overall UI checkpoint even though its inline detail rendering worked. Upper/middle/lower result selection stayed on Search and displayed the normalized ID plus clean canonical URL, but returning toward the sidebar could blank Search and leave only Home/Search reachable.

Source review of pinned Borealis `20e2d33b6c4ffce139ce304c503c04f5b94da920` identifies the unsafe interaction. `Box::removeView()` synchronously deletes result Views, while the inline implementation removed every result child and then called `Application::giveFocus()` on a replacement result. More importantly, TabFrame constructs a tab by calling its creator before attaching the returned page. When Search was re-entered from the sidebar with a selected identity, `create_page(Search)` rebuilt results and force-focused the selected result during the SidebarItem activation callback. That stole focus out of the sidebar before Down could continue to Subscriptions.

The Sections/B path also targeted the Sidebar container. Borealis resolves container focus via `getDefaultFocus()`, which selects the first sidebar item (Home); activating Home causes TabFrame to synchronously remove the Search page. The replacement resolves the actual active SidebarItem instead, so B from Search does not change the active tab or destroy the Search page.

The candidate fix also removes selection-time list rebuilding entirely. Result buttons and metadata remain alive; each result owns a pre-created detail label whose `VISIBLE`/`GONE` state changes when selection changes. Full list rebuilding remains limited to genuinely new Search results or reconstructing a Search page after a real tab switch.

`Load more` remains disabled. Guest bootstrap and first-page Search acceptance remain unchanged.
'''
if '## Inline-detail focus/page-lifecycle regression — 2026-09-13' not in s:
    s += addition
p.write_text(s)

p = Path('docs/ROADMAP.md')
s = p.read_text()
old = '''- [x] Replace it with a one-frame-deferred inline selected-result detail block on the stable Search page
- [ ] Real-hardware inline result-detail acceptance
- [ ] Re-enable continuation / `Load more` as a separate hardware-gated slice'''
new = '''- [x] Replace it with an inline selected-result detail block on the stable Search page
- [x] Hardware-observe inline detail rendering at `36015cb...` but reject the overall UI checkpoint after blank-page/sidebar-focus regression
- [x] Remove destructive selection-time result-tree rebuilding and forced result refocus
- [x] Return B to the active SidebarItem instead of the Sidebar container/default Home item
- [ ] Real-hardware inline-detail + full sidebar traversal acceptance
- [ ] Re-enable continuation / `Load more` as a separate hardware-gated slice'''
s = replace_once(s, old, new, 'roadmap inline gate')
s = s.replace(
    '**Real-Switch guest bootstrap is accepted (`Guest ready`) and the startup-recovery shell has now physically booted without crashing. The current gate is first-page live Search reactivation on the accepted plain `ScrollingFrame` shell. Text-only normalized results and result selection are enabled; `Load more` is intentionally disabled until a later hardware slice. Preserve the exact `www.youtube.com:443` allowlist and hard Shorts/ad firewall. Post-v1 OAuth work remains documentation only during M2.**',
    '**Guest bootstrap, startup recovery and first-page live Search are physically accepted. The current gate is the inline-detail/sidebar lifecycle fix after `36015cb...` was rejected for blank Search content and Home/Search-only sidebar traversal. `Load more` remains disabled. Preserve the exact `www.youtube.com:443` allowlist and hard Shorts/ad firewall; post-v1 OAuth remains documentation only during M2.**')
p.write_text(s)
