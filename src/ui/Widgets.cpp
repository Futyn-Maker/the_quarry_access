#include "ui/Widgets.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"

#include <Unreal/FString.hpp>

#include <algorithm>
#include <cwctype>

namespace qa::ui
{
    namespace
    {
        // Widget classes that behave as controls, most specific first.
        struct KindRule
        {
            const wchar_t* className;
            Kind kind;
        };
        const KindRule kKinds[] = {
            {L"MenuCarousel_C", Kind::Selector},
            {L"DynamicMenuCarousel_C", Kind::Selector},
            {L"MenuCarousel_New_C", Kind::Selector},
            {L"MovieModeSetting_C", Kind::Selector},
            {L"TextSlider_C", Kind::Slider},
            {L"SMGSlider_C", Kind::Slider},
            {L"PauseTabWidget_C", Kind::Tab},
            {L"KeyRemap_C", Kind::Button},
            {L"MenuButton_C", Kind::Button},
            {L"BasicPopupButton_C", Kind::Button},
            {L"SaveSlotButtonBase_C", Kind::Button},
            {L"DnaDocumentButton_C", Kind::Button},
            {L"DnaPrimaryButton_C", Kind::Button},
            {L"DnaSecondaryButton_C", Kind::Button},
            {L"CollectablesButtonBase_C", Kind::Button},
            // The film strip of a path walks through moments that have already happened: it
            // is not a setting, so it is not announced as one.
            {L"PathChosenCarouselBase_C", Kind::Other},
            {L"EditableTextBox", Kind::Edit},
            {L"EditableText", Kind::Edit},
            {L"UIInteractableWidgetSMG026_C", Kind::Other},
            {L"UIInteractableWidgetBaseSMG026", Kind::Other},
        };

        // Rows are laid out differently from one screen to the next, so the text they show
        // is first taken from the shallow part of the tree (where a control's own label
        // sits) and, when a row nests its label deeper, from the whole subtree.
        constexpr int kLabelDepth = 4;
        constexpr int kLabelDepthDeep = 12;

        // Text blocks that carry a control's name, for rows that keep it out of reach of
        // the tree walk (a collapsed wrapper, a stripped-down design-time tree).
        const wchar_t* const kLabelProperties[] = {L"TitleText", L"Name", L"Title", L"LabelText", L"NameText", L"ItemText"};

        std::wstring StripTrailingColon(std::wstring s)
        {
            s = str::Trim(s);
            while (!s.empty() && (s.back() == L':' || s.back() == L'：'))
                s.pop_back();
            return str::Trim(s);
        }

        // Text of a named text-block property, resolved from the widget's own strings.
        std::wstring TextProperty(UObject* widget, const wchar_t* property)
        {
            UObject* block = nullptr;
            if (!obj::ReadObject(widget, property, block) || !block) return {};
            return str::StripMarkup(obj::TextOf(block));
        }

        // A deep walk also meets the highlight dressing of a row: arrow markers and the
        // label repeated for a glow layer. Only the text that carries meaning is kept.
        std::vector<std::wstring> DisplayTexts(const std::vector<std::wstring>& texts)
        {
            std::vector<std::wstring> kept;
            for (const auto& raw : texts)
            {
                const auto text = str::CollapseWhitespace(str::StripMarkup(raw));
                if (text.empty() || text.find_first_not_of(L"<>«»‹›•·|-–—") == std::wstring::npos) continue;
                if (std::find(kept.begin(), kept.end(), text) != kept.end()) continue;
                kept.push_back(text);
            }
            return kept;
        }

        // The text of the control's own named text blocks, used when walking its tree
        // yields nothing.
        std::vector<std::wstring> NamedTexts(UObject* widget)
        {
            std::vector<std::wstring> texts;
            for (const wchar_t* property : kLabelProperties)
            {
                const auto text = str::Trim(TextProperty(widget, property));
                if (!text.empty()) texts.push_back(text);
            }
            return texts;
        }

        std::wstring ActionOf(UObject* promptWidget)
        {
            const auto action = ActionName(promptWidget, L"InputActionName");
            return action.empty() ? ActionName(promptWidget, L"ButtonInputMapping") : action;
        }

        // Widgets whose text lives in one property while their child text blocks carry
        // visual layers of it (highlight glow, arrow markers, a partly typed line).
        bool ReadTextLeaf(UObject* widget, std::wstring& out)
        {
            if (obj::IsA(widget, L"ScanlineText_C") || obj::IsA(widget, L"PathChosenTitle_C"))
            {
                obj::ReadString(widget, obj::IsA(widget, L"ScanlineText_C") ? L"Text" : L"TitleString", out);
                out = str::CollapseWhitespace(str::StripMarkup(out));
                return true;
            }
            if (obj::IsA(widget, L"TypeWriterTextBlockSMG026"))
            {
                obj::ReadString(widget, L"Text", out);
                if (str::Trim(out).empty()) out = obj::CallForText(widget, L"GetText");
                if (str::Trim(out).empty()) out = gametext::ReadLocalized(widget, L"LocaleText");
                out = str::CollapseWhitespace(str::StripMarkup(out));
                return true;
            }
            // A localised block draws the game's string for its key; its Text property stays
            // empty unless the game writes one there.
            if (obj::IsA(widget, L"SMGLocalisedTextBlock") || obj::IsA(widget, L"LocalisedTextBlockQuarry"))
            {
                out = str::StripMarkup(obj::TextOf(widget));
                if (str::Trim(out).empty()) out = gametext::ReadLocalized(widget, L"LocaleString");
                out = str::CollapseWhitespace(str::StripMarkup(out));
                return true;
            }
            // A text field holds what was typed only in the widget the game draws; its Text
            // property keeps the placeholder from the editor forever.
            if (obj::IsA(widget, L"EditableTextBox") || obj::IsA(widget, L"EditableText"))
            {
                out = EditField(widget).text;
                return true;
            }
            return false;
        }

        std::wstring PromptKey(UObject* promptWidget, const std::wstring& action)
        {
            // The glyph prints the key name for keyboard players; otherwise resolve the action.
            UObject* glyph = nullptr;
            if (obj::ReadObject(promptWidget, L"GlyphWidget", glyph) && glyph)
            {
                const auto printed = str::Trim(TextProperty(glyph, L"KeyTextBlock"));
                if (!printed.empty()) return input::KeyDisplayName(printed);
            }
            if (!action.empty()) return input::KeyForAction(action);
            return {};
        }

        // Everything a named block shows, read the way the walks read it, so a block that
        // keeps only a locale key or a bound getter gives its words like any other. Opacity
        // is not asked: a block is read as a screen opens, while it is still fading in.
        std::wstring BlockText(UObject* owner, const wchar_t* property)
        {
            UObject* block = nullptr;
            if (!obj::ReadObject(owner, property, block) || !obj::IsLive(block) || !obj::IsWidgetVisible(block)) return {};
            return str::CollapseWhitespace(str::Join(DisplayTexts(obj::DescendantTexts(block, kLabelDepthDeep)), L" "));
        }

        // Text of a named text block, but only when that block is actually shown. Prompt
        // widgets keep a hidden placeholder block on the unused side, which must not be read.
        std::wstring VisibleTextProperty(UObject* widget, const wchar_t* property)
        {
            UObject* block = nullptr;
            if (!obj::ReadObject(widget, property, block) || !block || !obj::IsLive(block)) return {};
            if (!obj::IsWidgetVisible(block)) return {};
            return str::StripMarkup(obj::TextOf(block));
        }

        // The prompt is rendered as "left text [glyph] right text"; the key name goes where the glyph is.
        std::wstring ComposePrompt(UObject* promptWidget, const std::wstring& key)
        {
            // A prompt that collapses its words shows its key alone; the words it would
            // fall back to are the placeholder from the editor.
            bool collapsed = false;
            if (obj::ReadBool(promptWidget, L"CollapseText", collapsed) && collapsed) return {};
            const auto left = str::Trim(VisibleTextProperty(promptWidget, L"PromptTextLeft"));
            const auto right = str::Trim(VisibleTextProperty(promptWidget, L"PromptTextRight"));
            std::wstring label;
            if (!left.empty() || !right.empty())
            {
                label = left;
                if (!key.empty()) label += (label.empty() ? L"" : L" ") + key;
                if (!right.empty())
                {
                    const bool punctuation = right.front() == L',' || right.front() == L'.' || right.front() == L'!' || right.front() == L'?';
                    label += (label.empty() || punctuation ? L"" : L" ") + right;
                }
                return str::CollapseWhitespace(label);
            }
            label = gametext::ReadLocalized(promptWidget, L"Localised Text", L"Text");
            if (label.empty()) return {};
            if (label.find(L"$(prompt)") != std::wstring::npos) return gametext::SubstitutePrompt(label, key.empty() ? locale::Mod(L"input.anykey") : key);
            return str::CollapseWhitespace(key.empty() ? label : label + L" " + key);
        }

        // Position of a row widget among the rows of its kind in the same panel.
        int RowPosition(UObject* row, int& count)
        {
            count = 0;
            UObject* slot = nullptr;
            UObject* parent = nullptr;
            if (!obj::ReadObject(row, L"Slot", slot) || !slot) return 0;
            if (!obj::ReadObject(slot, L"Parent", parent) || !parent) return 0;
            const auto rowClass = obj::ClassName(row);
            int index = 0;
            for (auto* child : obj::PanelChildren(parent))
            {
                if (obj::ClassName(child) != rowClass || !obj::IsWidgetVisible(child)) continue;
                ++count;
                if (child == row) index = count;
            }
            return index;
        }

        // One line of the panel beside a list of clues. A line the story has not unlocked,
        // and one the player has not looked at yet, shows its mark ("?", "!") in place of
        // its words: the game leaves the words there and draws them at no opacity. Opacity
        // cannot be read for this, since every line fades in when the panel is rebuilt, so
        // the line's own state decides, exactly as the game decides it.
        std::wstring CollectableLine(UObject* line)
        {
            bool unlocked = true;
            bool fresh = false;
            obj::ReadBool(line, L"bIsCollectableUnlocked", unlocked);
            obj::ReadBool(line, L"bIsCollectableNew", fresh);
            // A line that shows a picture from the scene hides its words behind it; the game
            // draws that box only when the line has a picture to put in it.
            UObject* picture = nullptr;
            const bool shows = obj::ReadObject(line, L"VideoBox", picture) && obj::IsLive(picture) && obj::IsWidgetVisible(picture);
            if (unlocked && !fresh && !shows) return str::Join(DisplayTexts(obj::DescendantTexts(line, kLabelDepthDeep)), L", ");
            UObject* mark = nullptr;
            if (!obj::ReadObject(line, L"StatusText", mark) || !obj::IsLive(mark) || !obj::IsWidgetVisible(mark)) return {};
            return str::Join(DisplayTexts(obj::DescendantTexts(mark, kLabelDepthDeep)), L" ");
        }

        int SiblingPosition(UObject* interactable, int& count)
        {
            count = 0;
            UObject* slot = nullptr;
            UObject* parent = nullptr;
            if (!obj::ReadObject(interactable, L"Slot", slot) || !slot) return 0;
            if (!obj::ReadObject(slot, L"Parent", parent) || !parent) return 0;
            int index = 0;
            for (auto* child : obj::PanelChildren(parent))
            {
                if (!IsInteractable(child) || !obj::IsWidgetVisible(child)) continue;
                ++count;
                if (child == interactable) index = count;
            }
            return index;
        }
    }

    void Install()
    {
        obj::SetTextLeafReader(&ReadTextLeaf);
    }

    std::wstring ActionName(UObject* widget, std::wstring_view property)
    {
        void* ref = obj::StructPtr(widget, property); // FActionMappingReference { FString Value; }
        if (!ref) return {};
        auto* value = static_cast<RC::Unreal::FString*>(ref);
        if (value->Len() <= 0) return {};
        const auto* chars = **value;
        return chars ? std::wstring(chars) : std::wstring();
    }

    EditState EditField(UObject* field)
    {
        EditState edit;
        bool password = false;
        edit.hidden = obj::ReadBool(field, L"IsPassword", password) && password;
        const auto text = str::CollapseWhitespace(str::StripMarkup(obj::CallForText(field, L"GetText")));
        edit.length = text.size();
        // A password field draws dots rather than letters, so its letters are not said either.
        if (!edit.hidden) edit.text = text;
        return edit;
    }

    Kind KindOf(UObject* interactable)
    {
        for (const auto& rule : kKinds)
        {
            if (obj::IsA(interactable, rule.className)) return rule.kind;
        }
        return Kind::Other;
    }

    bool IsInteractable(UObject* widget)
    {
        if (!widget) return false;
        for (const auto& rule : kKinds)
        {
            if (obj::IsA(widget, rule.className)) return true;
        }
        return false;
    }

    UObject* Interactable(UObject* focused)
    {
        // A control can keep a highlighted part inside it (a popup button holds a panel
        // that lights up on its own); the control with a kind of its own wins over such a
        // part, which is taken only when nothing above it is known.
        UObject* generic = nullptr;
        UObject* cur = focused;
        for (int depth = 0; cur && depth < 12; ++depth)
        {
            if (IsInteractable(cur))
            {
                if (KindOf(cur) != Kind::Other) return cur;
                if (!generic) generic = cur;
            }
            cur = obj::FindOuterUserWidget(cur, 12);
        }
        return generic;
    }

    Description Describe(UObject* interactable)
    {
        Description d;
        d.widget = interactable;
        if (!interactable) return d;
        d.kind = KindOf(interactable);

        // A line of the panel beside a list is read as the game draws it: its words, or the
        // mark that stands in for them while the story keeps them back.
        if (obj::IsA(interactable, L"CollectablesInfoButton_C"))
        {
            d.label = str::CollapseWhitespace(str::StripMarkup(CollectableLine(interactable)));
            d.index = SiblingPosition(interactable, d.count);
            return d;
        }

        // The film strip of a path: it keeps the focus on itself while the arrows move its
        // current moment. The moments are pictures of the scenes they come from and carry no
        // words; the line about the moment the strip stands on is written above it.
        if (obj::IsA(interactable, L"PathChosenCarouselBase_C"))
        {
            UObject* title = nullptr;
            if (obj::ReadObject(interactable, L"CarouselTitle", title) && obj::IsLive(title))
            {
                d.label = StripTrailingColon(TextProperty(title, L"Title"));
                std::wstring own;
                if (str::Trim(d.label).empty() && ReadTextLeaf(title, own)) d.label = StripTrailingColon(own);
            }
            UObject* current = nullptr;
            int64_t index = -1;
            int64_t count = 0;
            obj::ReadObject(interactable, L"CurrentButton", current);
            obj::ReadInt(interactable, L"ButtonCount", count);
            if (obj::IsLive(current)) obj::ReadInt(current, L"ButtonIndex", index);
            std::vector<std::wstring> parts{str::CollapseWhitespace(str::StripMarkup(VisibleTextProperty(interactable, L"TitleText")))};
            if (index >= 0 && count > 0) parts.push_back(locale::Mod(L"ui.pos", std::to_wstring(index + 1), std::to_wstring(count)));
            d.value = str::Join(DisplayTexts(parts), L", ");
            // A strip of pictures leaves nothing else to check afterwards, so what it gave is
            // logged as it changes.
            static UObject* lastCarousel = nullptr;
            static int64_t lastIndex = -2;
            if (interactable != lastCarousel || index != lastIndex)
            {
                lastCarousel = interactable;
                lastIndex = index;
                log::Info(L"paths: {} moment {} of {}: {}", d.label, index + 1, count, d.value);
            }
            return d;
        }

        // Label: the text the game actually renders (already localized), split into a
        // title and a value for selectors and sliders. The internal Text field holds a
        // developer/English placeholder and is only a last resort.
        const auto rendered = obj::DescendantTexts(interactable, kLabelDepth);
        if (d.kind == Kind::Selector || d.kind == Kind::Slider)
        {
            // The row title comes from its own text block: in the rendered order the current
            // value often comes first, so position cannot be used to tell them apart.
            d.label = str::CollapseWhitespace(str::StripMarkup(TextProperty(interactable, L"TitleText")));
            // A carousel placed inside a row widget (the director's chair settings) is titled
            // by that row.
            if (str::Trim(d.label).empty())
            {
                if (UObject* row = obj::FindOuterUserWidget(interactable))
                {
                    for (const wchar_t* property : {L"Title", L"TitleText"})
                    {
                        if (str::Trim(d.label).empty()) d.label = str::CollapseWhitespace(str::StripMarkup(VisibleTextProperty(row, property)));
                    }
                }
            }
            if (d.kind == Kind::Slider) d.value = TextProperty(interactable, L"ValueText");
            if (str::Trim(d.value).empty())
            {
                std::vector<std::wstring> values;
                for (const auto& text : rendered)
                {
                    if (str::CollapseWhitespace(str::StripMarkup(text)) != d.label) values.push_back(text);
                }
                d.value = str::Join(values, L" ");
            }
            if (str::Trim(d.label).empty() && !rendered.empty()) d.label = rendered.front();
            // A selector with no title of its own shows its value alone (the Wolf Pack host's
            // mode), which is then its name and not to be said twice.
            if (str::EqualsNoCase(str::Trim(d.value), str::Trim(d.label))) d.value.clear();
        }
        else if (d.kind == Kind::Edit)
        {
            // A text field is named by the hint it shows while it is empty, and says what
            // it holds. The hint is bound to a getter, so the property itself is empty.
            d.label = obj::BoundText(interactable, L"HintText");
            if (str::Trim(d.label).empty()) obj::ReadString(interactable, L"HintText", d.label);
            d.value = EditField(interactable).text;
        }
        else
        {
            // Buttons, tabs, save slots: everything the control shows (name, chapter, duration, ...).
            d.label = str::Join(rendered, L", ");
        }
        // Menu buttons and pause tabs keep their text below the shallow walk but carry the
        // locale key of the game itself, which is the cleanest name there is.
        if (str::Trim(d.label).empty()) d.label = gametext::ReadLocalized(interactable, L"LocalisedText");
        if (str::Trim(d.label).empty()) d.label = gametext::ReadLocalized(interactable, L"ItemTextLoc");
        // Rows without a key (key bindings, tutorials, the character tab) are read from
        // their whole subtree, or from their named text blocks. A text field is left
        // nameless instead: its subtree and its Text hold what it contains, not its name.
        if (str::Trim(d.label).empty() && d.kind != Kind::Edit)
        {
            auto deep = DisplayTexts(obj::DescendantTexts(interactable, kLabelDepthDeep));
            if (deep.empty()) deep = NamedTexts(interactable);
            d.label = str::Join(deep, L", ");
            // Last of all the unlocalized strings the designers typed in.
            if (str::Trim(d.label).empty()) obj::ReadString(interactable, L"ItemTextUnloc", d.label);
            if (str::Trim(d.label).empty()) obj::ReadString(interactable, L"Text", d.label);
        }
        d.label = str::CollapseWhitespace(str::StripMarkup(d.label));

        // There is no state the whole game shares for a control that cannot be used: each
        // family of controls keeps its own flag, and every one of them is asked.
        bool disabled = false;
        if ((obj::ReadBool(interactable, L"bIsDisabled", disabled) && disabled) || (obj::ReadBool(interactable, L"bIsGreyed", disabled) && disabled) ||
            (obj::ReadBool(interactable, L"IsDisabled", disabled) && disabled))
        {
            d.state = locale::Mod(L"ui.state.disabled");
        }
        // A collectable the story has not reached cannot be opened, and the game draws that
        // in one of two ways: it dims the row, keeping its name (the tutorial videos), or it
        // puts the placeholder in place of the name (the lists of clues, evidence and tarot
        // cards). Where the name already stands for it, it is not said twice.
        bool unlockedItem = true;
        if (obj::ReadBool(interactable, L"bIsCollectableUnlocked", unlockedItem) && !unlockedItem)
        {
            const auto placeholder = str::CollapseWhitespace(str::StripMarkup(gametext::ReadLocalized(interactable, L"HiddenNameString")));
            const bool nameSaysIt = str::EqualsNoCase(placeholder, d.label) ||
                                    std::none_of(d.label.begin(), d.label.end(), [](wchar_t c) { return std::iswalnum(static_cast<wint_t>(c)) != 0; });
            if (!nameSaysIt) d.state = locale::Mod(L"ui.state.disabled");
        }
        bool locked = false;
        if (obj::ReadBool(interactable, L"bIsUnlocked", locked) && !locked && obj::FindProperty(interactable, L"bIsUnlocked"))
        {
            d.state = locale::Mod(L"ui.state.locked");
        }

        d.index = SiblingPosition(interactable, d.count);
        if (d.count <= 1)
        {
            if (UObject* row = obj::FindOuterUserWidget(interactable))
            {
                int rowCount = 0;
                const int rowIndex = RowPosition(row, rowCount);
                if (rowCount > 1)
                {
                    d.index = rowIndex;
                    d.count = rowCount;
                }
            }
        }

        d.tip = gametext::ReadLocalized(interactable, L"LocalizedTipText", L"TipText");
        // A list of clues, places or paths shows the item it is on in a panel beside it, all
        // at once: where it was found, then its lines. A line the story has not unlocked, or
        // one not yet looked at, is drawn as its mark alone, and is read so.
        if (str::Trim(d.tip).empty())
        {
            UObject* tab = obj::NearestAncestorOfClass(interactable, L"PauseTabCollectablesBase_C");
            UObject* current = nullptr;
            UObject* info = nullptr;
            if (tab && obj::ReadObject(tab, L"CurrentCollectablesButton", current) && current == interactable && obj::ReadObject(tab, L"InfoContent", info) &&
                obj::IsLive(info) && obj::IsWidgetShown(info))
            {
                std::vector<std::wstring> lines;
                UObject* title = nullptr;
                if (obj::ReadObject(tab, L"CollectablesTitle", title) && obj::IsLive(title))
                    lines.push_back(str::CollapseWhitespace(VisibleTextProperty(title, L"Subtitle")));
                for (auto* line : obj::PanelChildren(info))
                    lines.push_back(CollectableLine(line));
                d.tip = str::JoinSentences(lines);
            }
        }
        // A clue, piece of evidence, tarot card or tutorial carries its own description.
        UObject* collectable = nullptr;
        bool unlocked = true;
        if (str::Trim(d.tip).empty() && obj::ReadObject(interactable, L"Collectable", collectable) && obj::IsLive(collectable) &&
            (!obj::ReadBool(interactable, L"bIsCollectableUnlocked", unlocked) || unlocked))
        {
            d.tip = gametext::ReadLocalized(collectable, L"CollectableDescription");
        }
        // A selector explained by a text beside it rather than by a tooltip: the Wolf Pack
        // host's mode is described under the selector on the screen that holds it.
        for (UObject* outer = obj::FindOuterUserWidget(interactable); outer && str::Trim(d.tip).empty() && d.kind == Kind::Selector;
             outer = obj::FindOuterUserWidget(outer))
        {
            d.tip = VisibleTextProperty(outer, L"ModeDescription_txt");
        }
        d.tip = str::CollapseWhitespace(str::StripMarkup(d.tip));
        return d;
    }

    std::wstring Speak(const Description& d)
    {
        std::vector<std::wstring> parts;
        parts.push_back(d.label);
        switch (d.kind)
        {
        case Kind::Selector: parts.push_back(locale::Mod(L"ui.type.selector")); break;
        case Kind::Slider: parts.push_back(locale::Mod(L"ui.type.slider")); break;
        case Kind::Checkbox: parts.push_back(locale::Mod(L"ui.type.checkbox")); break;
        case Kind::Tab: parts.push_back(locale::Mod(L"ui.type.tab")); break;
        case Kind::Edit: parts.push_back(locale::Mod(L"ui.type.edit")); break;
        default: break;
        }
        if (!d.value.empty()) parts.push_back(d.value);
        if (!d.state.empty()) parts.push_back(d.state);
        // Where the selection stands in a list is counted by the mod, not shown by the game,
        // and it is the longest part of every row.
        if (cfg::Detailed() && d.index > 0 && d.count > 1) parts.push_back(locale::Mod(L"ui.pos", std::to_wstring(d.index), std::to_wstring(d.count)));
        if (!d.tip.empty() && d.tip != d.label) parts.push_back(d.tip);
        return str::Join(parts, L", ");
    }

    namespace
    {
        // Whether the game reports a pause tab as the screen on display. This, and not the
        // bar's own bIsActive, says that the pause menu is up: that flag is the negation of
        // IsFocusSuspended, so it drops whenever a popup or the film strip of a path takes
        // the focus, and stays raised long after the menu is gone. The answer may belong to
        // the tab or to the screen widget that holds it, so the tree above it is asked too.
        bool AskCurrentScreen(UObject* widget, bool& answered)
        {
            UObject* cur = widget;
            for (int depth = 0; obj::IsLive(cur) && depth < 8; ++depth)
            {
                if (obj::FindFunction(cur, L"IsCurrentScreen"))
                {
                    answered = true;
                    if (obj::CallForBool(cur, L"IsCurrentScreen")) return true;
                }
                UObject* parent = obj::ParentWidget(cur);
                cur = parent ? parent : obj::FindOuterUserWidget(cur);
            }
            return false;
        }

        bool PauseTabIsCurrent(const std::vector<UObject*>& shown, bool& answered)
        {
            answered = false;
            for (auto* w : shown)
            {
                if (AskCurrentScreen(w, answered)) return true;
            }
            return false;
        }

        std::vector<UObject*> ShownPauseTabs()
        {
            std::vector<UObject*> shown;
            for (auto* w : obj::FindAllLive(L"PauseScreenBaseSMG026"))
            {
                if (obj::IsWidgetShown(w)) shown.push_back(w);
            }
            return shown;
        }

        // The content the tab bar is showing. Contents of tabs visited earlier stay loaded
        // and shown for a while after leaving them, so being shown is not enough: the tab
        // widgets are named after their content ("CluesTab" shows PauseTabClues_C), and
        // failing that the game is asked which screen is current.
        UObject* PauseContent(UObject* system)
        {
            const auto shown = ShownPauseTabs();
            if (shown.empty()) return nullptr;
            if (shown.size() == 1) return shown.front();
            const auto tabs = PauseTabsOf(system);
            if (tabs.tab)
            {
                std::wstring name = obj::ObjectName(tabs.tab);
                if (str::EndsWith(name, L"Tab")) name.resize(name.size() - 3);
                if (name == L"Camp") name = L"Map";
                if (name == L"Character") name = L"Relationship";
                const std::wstring className = L"PauseTab" + name + L"_C";
                for (auto* w : shown)
                {
                    if (obj::ClassName(w) == className) return w;
                }
            }
            for (auto* w : shown)
            {
                if (obj::CallForBool(w, L"IsCurrentScreen")) return w;
            }
            return shown.front();
        }
    }

    PauseMenu PauseMenuState()
    {
        // Every reading of a screen asks for this, and answering means a scan of the object
        // array, so the answer is kept for the frame it was found in.
        static PauseMenu cached;
        static unsigned long long cachedFrame = 0;
        const auto frame = gamethread::FrameCount();
        if (frame != 0 && frame == cachedFrame) return cached;
        cachedFrame = frame;
        cached = PauseMenu{};
        const auto shown = ShownPauseTabs();
        if (shown.empty()) return cached;
        bool answered = false;
        const bool current = PauseTabIsCurrent(shown, answered);
        cached.answered = answered;
        // Without an answer from the game a drawn tab is taken for the menu being up: the
        // tabs are top-level widgets of their own and are not drawn outside it.
        if (answered && !current) return cached;
        cached.up = true;
        cached.system = PauseTabSystem();
        cached.content = PauseContent(cached.system);
        return cached;
    }

    bool IsPauseWidget(UObject* widget)
    {
        return obj::IsA(widget, L"PauseTabSystemSMG026") || obj::IsA(widget, L"PauseScreenBaseSMG026") || obj::IsA(widget, L"CharacterCarousel_C");
    }

    UObject* PauseAnchor(UObject* widget)
    {
        if (!obj::IsLive(widget) || !IsPauseWidget(widget)) return nullptr;
        // The tab bar and the character carousel come and go around the content of the tab on
        // display, and the whole menu is built afresh at every opening, so that content is
        // what the menu answers to. With none of it drawn there is no screen to speak of.
        const auto menu = PauseMenuState();
        return obj::IsLive(menu.content) ? menu.content : nullptr;
    }

    std::vector<UObject*> ScreenRoots(UObject* screen)
    {
        std::vector<UObject*> roots;
        const bool pauseWidget = !obj::IsLive(screen) || IsPauseWidget(screen);
        const auto menu = pauseWidget ? PauseMenuState() : PauseMenu{};
        if (!menu.up)
        {
            if (obj::IsLive(screen)) roots.push_back(screen);
            return roots;
        }
        if (obj::IsLive(menu.system)) roots.push_back(menu.system);
        if (obj::IsLive(menu.content)) roots.push_back(menu.content);
        for (auto* carousel : obj::FindAllLive(L"CharacterCarousel_C"))
        {
            if (obj::IsWidgetShown(carousel)) roots.push_back(carousel);
        }
        return roots;
    }

    namespace
    {
        bool UnderRoots(UObject* widget, const std::vector<UObject*>& roots)
        {
            for (auto* root : roots)
            {
                if (obj::IsDescendantOf(widget, root)) return true;
            }
            return false;
        }

        // A widget that is on display: itself and everything above it visible, and part of
        // the screen being read.
        bool OnScreen(UObject* widget, const std::vector<UObject*>& roots)
        {
            return obj::IsLive(widget) && obj::IsWidgetShown(widget) && UnderRoots(widget, roots);
        }

        // Lines are joined as sentences, except that a heading ending in a colon keeps
        // its value on the same line ("Objective: Return to the road").
        std::wstring JoinLines(const std::vector<std::wstring>& parts)
        {
            std::wstring out;
            for (const auto& part : parts)
            {
                if (part.empty()) continue;
                if (out.empty())
                    out = part;
                else if (out.back() == L':' || out.back() == L'：' || out.back() == L'.' || out.back() == L'!' || out.back() == L'?' || out.back() == L'…')
                    out += L" " + part;
                else
                    out += L". " + part;
            }
            return out;
        }
    }

    std::wstring ScreenTitle(UObject* screen)
    {
        if (!screen) return {};
        const auto roots = ScreenRoots(screen);
        // A screen names its own title in a Title property: a menu title widget, or a text
        // block of its own (the 2K account screens). It comes first, since a screen may
        // head its lists with more title widgets (the Wolf Pack lobby's friends and lobby).
        for (auto* root : roots)
        {
            UObject* own = nullptr;
            if (!obj::ReadObject(root, L"Title", own) || !obj::IsLive(own) || !obj::IsWidgetShown(own)) continue;
            const auto text =
                StripTrailingColon(obj::IsA(own, L"MenuTitle_C") ? TextProperty(own, L"Title") : str::Join(DisplayTexts(obj::DescendantTexts(own, 0)), L" "));
            if (!text.empty()) return text;
        }
        // Otherwise, like the prompts, the title widget is located by walking up from the
        // live instances rather than down from the screen.
        for (auto* title : obj::FindAllLive(L"MenuTitle_C"))
        {
            if (!OnScreen(title, roots)) continue;
            const auto text = StripTrailingColon(TextProperty(title, L"Title"));
            if (!text.empty()) return text;
        }
        for (auto* content : obj::FindAllLive(L"PopupContent_C"))
        {
            if (!OnScreen(content, roots)) continue;
            const auto text = StripTrailingColon(TextProperty(content, L"PopupTitle"));
            if (!text.empty()) return text;
        }
        // The headline of a list tab (the clue tab, the paths tab) is not a title of the
        // screen but the name of the item the selection is on, which is read with that item.
        return {};
    }

    namespace
    {
        // Texts spoken elsewhere: the labels of the controls, the prompts, the tabs.
        std::vector<std::wstring> SpokenElsewhere(const std::vector<UObject*>& roots)
        {
            std::vector<std::wstring> texts;
            auto collect = [&](UObject* w)
            {
                for (const auto& text : obj::DescendantTexts(w, kLabelDepthDeep))
                    texts.push_back(str::CollapseWhitespace(str::StripMarkup(text)));
            };
            // The controls, the prompt bar, the description line, the pause tabs and the
            // headline naming the item the selection is on: each is read in its own place.
            for (const wchar_t* className : {L"UIInteractableWidgetBaseSMG026", L"EditableTextBox", L"MenuPromptWidget_C", L"MenuBarBottom_C",
                                             L"PauseTabWidget_C", L"CollectablesTitle_C", L"PathChosenTitle_C"})
            {
                // Taken whether or not they are drawn this moment: a bar fading in or out
                // still holds a placeholder the body must not pick up.
                for (auto* widget : obj::FindAllLive(className))
                {
                    if (obj::IsLive(widget) && UnderRoots(widget, roots)) collect(widget);
                }
            }
            // The description beside a selector is read with that selector.
            for (auto* root : roots)
            {
                UObject* description = nullptr;
                if (obj::ReadObject(root, L"ModeDescription_txt", description) && obj::IsLive(description)) collect(description);
            }
            return texts;
        }
    }

    namespace
    {
        // "Objective: Return to the road" — heading first, whatever order the widgets have.
        std::wstring ObjectiveLine(UObject* objective)
        {
            std::vector<std::wstring> parts;
            UObject* heading = nullptr;
            if (obj::ReadObject(objective, L"CollectablesGroupTitle", heading) && obj::IsLive(heading) && obj::IsWidgetShown(heading))
            {
                for (const auto& text : DisplayTexts(obj::DescendantTexts(heading, kLabelDepthDeep)))
                    parts.push_back(text);
            }
            const auto text = str::CollapseWhitespace(VisibleTextProperty(objective, L"ObjectiveText"));
            if (!text.empty()) parts.push_back(text);
            return JoinLines(parts);
        }
    }

    std::wstring ScreenBody(UObject* screen)
    {
        if (!screen) return {};
        const auto roots = ScreenRoots(screen);
        const auto title = ScreenTitle(screen);
        const auto elsewhere = SpokenElsewhere(roots);
        std::vector<std::wstring> parts;
        // The title is spoken before the body, so the block it came from, and each part of a
        // title made of several, are left out of the screen's own text.
        const auto inTitle = [&](const std::wstring& text)
        {
            const auto part = StripTrailingColon(text);
            return part == title || title.starts_with(part + L", ") || title.ends_with(L", " + part);
        };
        auto add = [&](const std::wstring& text)
        {
            if (text.empty() || inTitle(text)) return;
            if (std::find(elsewhere.begin(), elsewhere.end(), text) != elsewhere.end()) return;
            if (std::find(parts.begin(), parts.end(), text) == parts.end()) parts.push_back(text);
        };
        // Couch co-op warns that players were left without a character and names them between
        // the two halves of its message, one row apiece. A row is a widget of its own, too
        // deep under the popup for the walk over the popup's own text to reach, so the first
        // half and the names are taken in order here and the rest follows below.
        for (auto* root : roots)
        {
            if (!obj::IsA(root, L"CouchCo-opNoCharactersAssigned_C")) continue;
            add(BlockText(root, L"BodyText_pt1"));
            for (auto* row : obj::FindAllLive(L"CouchCo-opQuickStartPlayer_C"))
            {
                if (OnScreen(row, roots)) add(BlockText(row, L"PlayerName"));
            }
        }
        for (auto* container : obj::FindAllLive(L"PopupContent_C"))
        {
            if (!OnScreen(container, roots)) continue;
            for (const auto& text : DisplayTexts(obj::DescendantTexts(container, kLabelDepthDeep)))
                add(text);
        }
        for (auto* objective : obj::FindAllLive(L"Objective_C"))
        {
            if (OnScreen(objective, roots)) add(ObjectiveLine(objective));
        }
        for (auto* carousel : obj::FindAllLive(L"CharacterCarousel_C"))
        {
            if (OnScreen(carousel, roots)) add(CarouselText(carousel));
        }
        // The controller handover of couch co-op: whose turn it is, and as whom, above the
        // button that answers it.
        for (auto* handover : obj::FindAllLive(L"CouchCo-opHandover_C"))
        {
            if (!OnScreen(handover, roots)) continue;
            std::vector<std::wstring> who;
            for (const wchar_t* property : {L"PlayerName", L"CharacterName"})
            {
                const auto text = str::CollapseWhitespace(PropertyText(handover, property));
                if (!text.empty() && std::find(who.begin(), who.end(), text) == who.end()) who.push_back(text);
            }
            add(str::Join(who, L", "));
        }
        // Waiting for the rest of a Wolf Pack: the line the screen shows.
        for (auto* waiting : obj::FindAllLive(L"WolfPackWaitSyncWidget_C"))
        {
            if (!OnScreen(waiting, roots)) continue;
            for (const auto& text : DisplayTexts(obj::DescendantTexts(waiting, kLabelDepthDeep)))
                add(text);
        }
        // A screen that says its piece in text blocks of its own rather than through one of
        // the containers above (the 2K account screens) is read from its own tree, minus
        // everything that is already spoken in its own place.
        if (parts.empty())
        {
            for (auto* root : roots)
            {
                for (const auto& text : DisplayTexts(obj::DescendantTexts(root, kLabelDepthDeep)))
                    add(text);
            }
        }
        // A selector the tab keys turn wherever the focus is belongs to the screen rather
        // than to the selection, so its setting is part of what the screen says.
        for (auto* selector : TabSelectors(screen))
            add(Speak(Describe(selector)));
        return JoinLines(parts);
    }

    std::vector<UObject*> TabSelectors(UObject* screen)
    {
        std::vector<UObject*> selectors;
        if (!screen) return selectors;
        const auto roots = ScreenRoots(screen);
        for (auto* selector : obj::FindAllLive(L"MenuCarousel_New_C"))
        {
            if (!OnScreen(selector, roots)) continue;
            for (const wchar_t* property : {L"PromptNext", L"PromptPrev"})
            {
                UObject* prompt = nullptr;
                if (obj::ReadObject(selector, property, prompt) && obj::IsLive(prompt) && obj::IsWidgetShown(prompt))
                {
                    selectors.push_back(selector);
                    break;
                }
            }
        }
        return selectors;
    }

    bool HasControls(UObject* screen)
    {
        if (!screen) return false;
        const auto roots = ScreenRoots(screen);
        for (const wchar_t* className : {L"UIInteractableWidgetBaseSMG026", L"EditableTextBox", L"PathChosenCarouselBase_C"})
        {
            for (auto* control : obj::FindAllLive(className))
            {
                if (IsInteractable(control) && KindOf(control) != Kind::Tab && OnScreen(control, roots)) return true;
            }
        }
        return false;
    }

    bool HasTextField(UObject* screen)
    {
        if (!screen) return false;
        const auto roots = ScreenRoots(screen);
        for (auto* field : obj::FindAllLive(L"EditableTextBox"))
        {
            if (OnScreen(field, roots)) return true;
        }
        return false;
    }

    std::wstring ScreenText(UObject* screen)
    {
        if (!screen) return {};
        const auto roots = ScreenRoots(screen);
        // Prompts close the readout on their own, tabs are its heading, the objective is
        // composed in its own order, and the bottom bar holds a placeholder behind the
        // line it really shows.
        std::vector<std::wstring> skip;
        for (const wchar_t* className : {L"MenuPromptWidget_C", L"PauseTabWidget_C", L"Objective_C", L"MenuBarBottom_C"})
        {
            for (auto* w : obj::FindAllLive(className))
            {
                if (!OnScreen(w, roots)) continue;
                for (const auto& text : obj::DescendantTexts(w, kLabelDepthDeep))
                    skip.push_back(str::CollapseWhitespace(str::StripMarkup(text)));
            }
        }
        std::vector<std::wstring> parts;
        size_t length = 0;
        auto add = [&](const std::wstring& text)
        {
            if (text.empty() || std::find(skip.begin(), skip.end(), text) != skip.end()) return;
            if (std::find(parts.begin(), parts.end(), text) != parts.end()) return;
            parts.push_back(text);
            length += text.size();
        };
        for (auto* objective : obj::FindAllLive(L"Objective_C"))
        {
            if (OnScreen(objective, roots)) add(ObjectiveLine(objective));
        }
        for (auto* root : roots)
        {
            std::vector<std::wstring> texts;
            if (obj::IsA(root, L"CharacterCarousel_C"))
                texts.push_back(CarouselText(root));
            else
                texts = DisplayTexts(obj::DescendantTexts(root, 16));
            for (const auto& text : texts)
            {
                if (length > 1500) break;
                add(text);
            }
        }
        add(ContextLine(screen));
        return JoinLines(parts);
    }

    UObject* CarouselCharacter(UObject* carousel)
    {
        UObject* item = nullptr;
        UObject* info = nullptr;
        if (!obj::ReadObject(carousel, L"CurrentCarouselItem", item) || !obj::IsLive(item)) return nullptr;
        if (!obj::ReadObject(item, L"CharacterInfo", info) || !obj::IsLive(info)) return nullptr;
        return info;
    }

    std::wstring CharacterName(UObject* characterInfo)
    {
        return obj::IsLive(characterInfo) ? str::CollapseWhitespace(gametext::ReadLocalized(characterInfo, L"CharacterName")) : std::wstring();
    }

    std::wstring PlayerOfCharacter(UObject* characterInfo, bool& onCouchScreen)
    {
        onCouchScreen = false;
        std::wstring player;
        // Each player of couch co-op keeps the characters given to them as name chips, and a
        // chip holds the character it names, so the owner is told by which chip is the same
        // character and never by comparing the names on screen.
        for (auto* button : obj::FindAllLive(L"CouchCo-opPlayerButton_C"))
        {
            if (!obj::IsWidgetShown(button)) continue;
            onCouchScreen = true;
            if (!obj::IsLive(characterInfo) || !player.empty()) continue;
            bool holds = false;
            obj::WalkWidgetTree(button, 10,
                                [&](UObject* widget, int)
                                {
                                    UObject* info = nullptr;
                                    if (obj::IsA(widget, L"CouchCo-opCharacterName_C") && obj::ReadObject(widget, L"CharacterInfo", info) &&
                                        info == characterInfo)
                                        holds = true;
                                    return !holds;
                                });
            if (!holds) continue;
            player = str::CollapseWhitespace(str::StripMarkup(TextProperty(button, L"ItemScanlineText")));
            if (player.empty()) obj::ReadString(button, L"Text", player);
        }
        return player;
    }

    std::wstring CarouselText(UObject* carousel)
    {
        if (!obj::IsLive(carousel)) return {};
        std::vector<std::wstring> parts;
        auto add = [&](const std::wstring& text)
        {
            if (!text.empty() && std::find(parts.begin(), parts.end(), text) == parts.end()) parts.push_back(text);
        };
        UObject* title = nullptr;
        if (obj::ReadObject(carousel, L"Title", title) && obj::IsLive(title) && obj::IsWidgetShown(title))
        {
            for (const wchar_t* property : {L"Title", L"Subtitle"})
                add(str::CollapseWhitespace(VisibleTextProperty(title, property)));
        }
        UObject* item = nullptr;
        if (obj::ReadObject(carousel, L"CurrentCarouselItem", item) && obj::IsLive(item) && obj::IsWidgetShown(item))
        {
            for (const auto& text : DisplayTexts(obj::DescendantTexts(item, 16)))
                add(text);
        }
        // Where characters are shared out between players, who has this one belongs with the
        // name and before the lines about what they are like. The name comes from the title
        // beside the carousel on one screen and from the card itself on another, so it goes
        // after whatever came first rather than at a place in either of them.
        bool couchCoop = false;
        const auto player = PlayerOfCharacter(CarouselCharacter(carousel), couchCoop);
        if (couchCoop)
        {
            const auto line = player.empty() ? locale::Mod(L"couch.free") : player;
            if (parts.empty())
                parts.push_back(line);
            else if (parts.front().find(line) == std::wstring::npos)
                parts.front() += L": " + line;
        }
        return JoinLines(parts);
    }

    UObject* PauseTabSystem()
    {
        // The game builds the pause menu anew at every opening and leaves the widgets of the
        // last one alive until they are collected, half a minute later. A remembered bar is
        // therefore kept only while it is the one being drawn; otherwise the menu would be
        // read from a bar that is no longer on screen, with no tabs to report.
        static UObject* known = nullptr;
        static unsigned long long scannedFrame = 0;
        if (obj::IsLive(known) && obj::IsWidgetShown(known)) return known;
        const auto frame = gamethread::FrameCount();
        if (frame != 0 && frame == scannedFrame) return nullptr;
        scannedFrame = frame;
        known = nullptr;
        for (auto* system : obj::FindAllLive(L"PauseTabSystemSMG026"))
        {
            if (!obj::IsWidgetShown(system)) continue;
            known = system;
            break;
        }
        return known;
    }

    PauseTabs PauseTabsOf(UObject* system)
    {
        PauseTabs tabs;
        // Only the drawn bar has tabs to report. Its bIsActive is not asked: it says whether
        // the bar has the focus this instant, not whether the menu is up.
        if (!obj::IsLive(system) || !obj::IsWidgetShown(system)) return tabs;
        tabs.system = system;
        int64_t selected = -1;
        obj::ReadInt(tabs.system, L"SelectedTab", selected);
        // The tabs are taken in the order the tab keys walk them; hidden ones (modes
        // without that tab) do not count.
        std::vector<std::pair<int64_t, UObject*>> visible;
        obj::WalkWidgetTree(tabs.system, 12,
                            [&](UObject* w, int)
                            {
                                int64_t index = 0;
                                if (w != tabs.system && obj::IsA(w, L"PauseTabWidget_C") && obj::IsWidgetShown(w))
                                {
                                    obj::ReadInt(w, L"TabIndex", index);
                                    visible.emplace_back(index, w);
                                }
                                return true;
                            });
        std::stable_sort(visible.begin(), visible.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        tabs.count = static_cast<int>(visible.size());
        for (int i = 0; i < tabs.count; ++i)
        {
            bool highlighted = false;
            obj::ReadBool(visible[i].second, L"bIsHighlighted", highlighted);
            if (visible[i].first == selected || (selected < 0 && highlighted))
            {
                tabs.tab = visible[i].second;
                tabs.index = i + 1;
                break;
            }
        }
        if (!tabs.tab) return tabs;
        tabs.label = Describe(tabs.tab).label;
        return tabs;
    }

    PauseTabs ActivePauseTabs()
    {
        const auto menu = PauseMenuState();
        return menu.up ? PauseTabsOf(menu.system) : PauseTabs{};
    }

    std::wstring PauseTabLine()
    {
        const auto tabs = ActivePauseTabs();
        if (tabs.label.empty()) return {};
        std::vector<std::wstring> parts{tabs.label, locale::Mod(L"ui.type.tab")};
        if (tabs.index > 0 && tabs.count > 1) parts.push_back(locale::Mod(L"ui.pos", std::to_wstring(tabs.index), std::to_wstring(tabs.count)));
        return str::Join(parts, L", ");
    }

    std::wstring HudText(UObject* instance)
    {
        if (!obj::IsLive(instance)) return {};
        std::vector<std::wstring> parts;
        if (obj::IsA(instance, L"HotLinkedNotificationBaseSMG026"))
        {
            // Title, headline and detail line, then the prompt that opens the pause tab.
            for (const wchar_t* property : {L"Title", L"PathChosenTitle", L"ShortText1", L"LongText1"})
            {
                UObject* block = nullptr;
                if (!obj::ReadObject(instance, property, block) || !obj::IsLive(block) || !obj::IsWidgetShown(block)) continue;
                for (const auto& text : DisplayTexts(obj::DescendantTexts(block, kLabelDepthDeep)))
                {
                    if (std::find(parts.begin(), parts.end(), text) == parts.end()) parts.push_back(text);
                }
            }
            bool hotlink = false;
            if (obj::ReadBool(instance, L"bHotLinkEnabled", hotlink) && hotlink)
            {
                const auto prompt = PromptText(instance);
                if (!prompt.empty()) parts.push_back(prompt);
            }
            return str::Join(parts, L". ");
        }
        return str::Join(DisplayTexts(obj::DescendantTexts(instance, kLabelDepthDeep)), L". ");
    }

    std::wstring PropertyText(UObject* widget, std::wstring_view property)
    {
        UObject* block = nullptr;
        if (!obj::ReadObject(widget, property, block) || !obj::IsLive(block) || !obj::IsWidgetShown(block, true)) return {};
        return str::Join(DisplayTexts(obj::DescendantTexts(block, kLabelDepthDeep)), L" ");
    }

    std::wstring PromptKeyName(UObject* promptWidget, std::wstring_view action)
    {
        return PromptKey(promptWidget, std::wstring(action));
    }

    std::wstring AxisPromptKeys(UObject* axisPrompt)
    {
        std::vector<std::wstring> keys;
        UObject* caps = nullptr;
        if (obj::ReadObject(axisPrompt, L"AxisInputKeys", caps) && obj::IsLive(caps) && obj::IsWidgetShown(caps, true))
        {
            for (const wchar_t* property : {L"KeyUp", L"KeyLeft", L"KeyDown", L"KeyRight"})
            {
                UObject* cap = nullptr;
                if (!obj::ReadObject(caps, property, cap) || !obj::IsLive(cap) || !obj::IsWidgetShown(cap, true)) continue;
                const auto key = PromptKey(cap, L"");
                if (!key.empty()) keys.push_back(key);
            }
        }
        if (keys.empty())
        {
            const auto glyph = PromptKey(axisPrompt, L"");
            if (!glyph.empty()) keys.push_back(glyph);
        }
        return str::Join(keys, L", ");
    }

    std::wstring GlyphKeyName(UObject* glyphWidget, std::wstring_view action)
    {
        if (!obj::IsLive(glyphWidget)) return {};
        const auto printed = str::Trim(TextProperty(glyphWidget, L"KeyTextBlock"));
        if (!printed.empty()) return input::KeyDisplayName(printed);
        return action.empty() ? std::wstring() : input::KeyForAction(action);
    }

    int64_t GameSettingValue(std::wstring_view assetName)
    {
        const std::wstring name(assetName);
        UObject* setting = obj::FindObject(L"/Game/UI/GameSettings/" + name + L"." + name);
        UObject* context = obj::LocalPlayerController();
        if (!setting || !context) return -1;
        auto* fn = obj::FindFunction(setting, L"GetCurrentEnumValueAsInt");
        if (!fn) return -1;
        int64_t value = -1;
        obj::Call(
            setting, fn,
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (prop && obj::PropertyTypeName(prop) == L"ObjectProperty") *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = context;
                }
            },
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (prop && prop->GetName() == L"ReturnValue") obj::ReadIntAt(params, prop, value);
                }
            });
        return value;
    }

    std::wstring PromptText(UObject* promptWidget)
    {
        const std::wstring action = ActionOf(promptWidget);
        std::wstring key = PromptKey(promptWidget, action);
        if (key.empty()) key = locale::Mod(L"input.anykey");
        return ComposePrompt(promptWidget, key);
    }

    std::vector<Prompt> Prompts(UObject* screen)
    {
        // Prompt widgets are added to their screen at runtime, so they are found by taking
        // every live prompt and keeping those that sit under this screen. Searching downwards
        // would depend on how deeply the screen nests them.
        std::vector<Prompt> prompts;
        if (!screen) return prompts;
        const auto roots = ScreenRoots(screen);
        std::wstring tabLeft;
        std::wstring tabRight;
        UObject* tabPrompt = nullptr;
        for (auto* w : obj::FindAllLive(L"MenuPromptWidget_C"))
        {
            if (!OnScreen(w, roots)) continue;
            Prompt p;
            p.action = ActionOf(w);
            p.key = PromptKey(w, p.action);
            p.label = ComposePrompt(w, p.key.empty() ? locale::Mod(L"input.anykey") : p.key);
            if (p.label.empty())
            {
                // The tab keys show their glyphs with no words beside them, on the pause
                // menu's tab bar and on the character and mode carousels alike.
                if (p.action == L"UITabLeft") tabLeft = p.key;
                if (p.action == L"UITabRight") tabRight = p.key;
                if (!tabPrompt) tabPrompt = w;
                continue;
            }
            const bool duplicate = std::any_of(prompts.begin(), prompts.end(), [&](const Prompt& e) { return e.label == p.label; });
            if (!duplicate) prompts.push_back(p);
        }
        // The wordless pair is named by what it turns on this screen, as the game names its
        // other prompts by what they do. At either end of its list a selector hides the
        // prompt that leads further, but the pair is still what turns it.
        if (!tabLeft.empty() || !tabRight.empty())
        {
            if (tabLeft.empty()) tabLeft = input::KeyForAction(L"UITabLeft");
            if (tabRight.empty()) tabRight = input::KeyForAction(L"UITabRight");
            const wchar_t* what = nullptr;
            if (obj::FindOuterOfClass(tabPrompt, L"PauseTabSystemSMG026"))
                what = L"ui.prompt.tabs";
            else if (obj::FindOuterOfClass(tabPrompt, L"CharacterCarousel_C"))
                what = L"help.carousel";
            else if (obj::FindOuterOfClass(tabPrompt, L"MenuCarousel_New_C"))
                what = L"ui.prompt.selector";
            if (what)
            {
                Prompt p;
                p.action = L"UITabRight";
                p.key = tabRight;
                p.label = locale::Mod(what, tabLeft, tabRight);
                prompts.push_back(p);
            }
        }
        return prompts;
    }

    std::wstring SpeakPrompts(const std::vector<Prompt>& prompts)
    {
        std::vector<std::wstring> parts;
        for (const auto& p : prompts)
        {
            if (!p.label.empty()) parts.push_back(p.label);
        }
        if (parts.empty()) return {};
        return str::Join(parts, L", ");
    }

    std::wstring ContextLine(UObject* screen)
    {
        if (!screen) return {};
        // The bar composes its line as it is drawn; the text block behind it keeps the
        // placeholder from the editor, so the line is asked for rather than read.
        const auto roots = ScreenRoots(screen);
        for (auto* bar : obj::FindAllLive(L"MenuBarBottom_C"))
        {
            if (!OnScreen(bar, roots)) continue;
            const auto text = str::CollapseWhitespace(str::StripMarkup(obj::CallForText(bar, L"GetUnlocalisedMenuContext")));
            if (!text.empty()) return text;
        }
        return {};
    }
}
