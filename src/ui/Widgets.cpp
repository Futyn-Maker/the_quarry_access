#include "ui/Widgets.hpp"

#include "core/Config.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"

#include <Unreal/FString.hpp>

#include <algorithm>

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
            {L"CollectablesButton_C", Kind::Button},
            {L"TutorialButton_C", Kind::Button},
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
            void* ref = obj::StructPtr(promptWidget, L"InputActionName"); // FActionMappingReference { FString Value; }
            if (!ref) return {};
            auto* value = static_cast<RC::Unreal::FString*>(ref);
            if (value->Len() <= 0) return {};
            const auto* chars = **value;
            return chars ? std::wstring(chars) : std::wstring();
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

    bool IsInteractable(UObject* widget)
    {
        if (!widget || !obj::IsA(widget, L"UserWidget")) return false;
        for (const auto& rule : kKinds)
        {
            if (obj::IsA(widget, rule.className)) return true;
        }
        return false;
    }

    UObject* Interactable(UObject* focused)
    {
        UObject* cur = focused;
        for (int depth = 0; cur && depth < 12; ++depth)
        {
            if (IsInteractable(cur)) return cur;
            cur = obj::FindOuterUserWidget(cur, 12);
        }
        return nullptr;
    }

    Description Describe(UObject* interactable)
    {
        Description d;
        d.widget = interactable;
        if (!interactable) return d;
        for (const auto& rule : kKinds)
        {
            if (obj::IsA(interactable, rule.className))
            {
                d.kind = rule.kind;
                break;
            }
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
        }
        else
        {
            // Buttons, tabs, save slots: everything the control shows (name, chapter, duration, ...).
            d.label = str::Join(rendered, L", ");
        }
        // Menu buttons keep their text below the shallow walk but carry the game's own
        // locale key, which is the cleanest name there is.
        if (str::Trim(d.label).empty()) d.label = gametext::ReadLocalized(interactable, L"LocalisedText");
        // Rows with neither (key bindings, tutorials) are read from their whole subtree,
        // or from their named text blocks.
        if (str::Trim(d.label).empty())
        {
            auto deep = DisplayTexts(obj::DescendantTexts(interactable, kLabelDepthDeep));
            if (deep.empty()) deep = NamedTexts(interactable);
            d.label = str::Join(deep, L", ");
        }
        if (str::Trim(d.label).empty()) obj::ReadString(interactable, L"Text", d.label);
        d.label = str::CollapseWhitespace(str::StripMarkup(d.label));

        bool disabled = false;
        if ((obj::ReadBool(interactable, L"bIsDisabled", disabled) && disabled) || (obj::ReadBool(interactable, L"bIsGreyed", disabled) && disabled))
        {
            d.state = locale::Mod(L"ui.state.disabled");
        }
        bool locked = false;
        if (obj::ReadBool(interactable, L"bIsUnlocked", locked) && !locked && obj::FindProperty(interactable, L"bIsUnlocked"))
        {
            d.state = locale::Mod(L"ui.state.locked");
        }

        d.index = SiblingPosition(interactable, d.count);

        d.tip = gametext::ReadLocalized(interactable, L"LocalizedTipText", L"TipText");
        d.tip = str::CollapseWhitespace(str::StripMarkup(d.tip));
        return d;
    }

    std::wstring Speak(const Description& d)
    {
        const auto verbosity = cfg::Get().verbosity;
        std::vector<std::wstring> parts;
        parts.push_back(d.label);
        if (verbosity != cfg::Verbosity::Minimal)
        {
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
        }
        if (verbosity == cfg::Verbosity::Full)
        {
            if (d.index > 0 && d.count > 1) parts.push_back(locale::Mod(L"ui.pos", std::to_wstring(d.index), std::to_wstring(d.count)));
            if (!d.tip.empty() && d.tip != d.label) parts.push_back(d.tip);
        }
        return str::Join(parts, L", ");
    }

    std::wstring ScreenTitle(UObject* screen)
    {
        if (!screen) return {};
        // Like the prompts, the title widget is located by walking up from the live
        // instances rather than down from the screen.
        for (auto* title : obj::FindAllLive(L"MenuTitle_C"))
        {
            if (!obj::IsWidgetVisible(title) || !obj::IsDescendantOf(title, screen)) continue;
            const auto text = StripTrailingColon(TextProperty(title, L"Title"));
            if (!text.empty()) return text;
        }
        for (auto* content : obj::FindAllLive(L"PopupContent_C"))
        {
            if (!obj::IsWidgetVisible(content) || !obj::IsDescendantOf(content, screen)) continue;
            const auto text = StripTrailingColon(TextProperty(content, L"PopupTitle"));
            if (!text.empty()) return text;
        }
        return {};
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
        for (auto* w : obj::FindAllLive(L"MenuPromptWidget_C"))
        {
            if (!obj::IsWidgetVisible(w)) continue;
            if (!obj::IsDescendantOf(w, screen)) continue;
            Prompt p;
            p.action = ActionOf(w);
            p.key = PromptKey(w, p.action);
            p.label = ComposePrompt(w, p.key.empty() ? locale::Mod(L"input.anykey") : p.key);
            if (p.label.empty()) continue;
            const bool duplicate = std::any_of(prompts.begin(), prompts.end(), [&](const Prompt& e) { return e.label == p.label; });
            if (!duplicate) prompts.push_back(p);
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
        for (auto* bar : obj::FindAllLive(L"MenuBarBottom_C"))
        {
            if (!obj::IsWidgetVisible(bar) || !obj::IsDescendantOf(bar, screen)) continue;
            const auto text = str::CollapseWhitespace(str::StripMarkup(obj::CallForText(bar, L"GetUnlocalisedMenuContext")));
            if (!text.empty()) return text;
        }
        return {};
    }
}
