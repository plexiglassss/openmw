#include "text.hpp"

#include "alignment.hpp"

namespace LuaUi
{
    LuaText::LuaText()
        : mAutoSized(true)
    {
    }

    void LuaText::initialize()
    {
        changeWidgetSkin("LuaText");
        setEditStatic(true);
        setVisibleHScroll(false);
        setVisibleVScroll(false);

        WidgetExtension::initialize();
    }

    void LuaText::updateProperties()
    {
        const bool oldAutoSize = mAutoSized;
        mAutoSized = propertyValue("autoSize", true);
        const MyGUI::UString caption(propertyValue("text", std::string()));
        const bool sameText = oldAutoSize == mAutoSized && hasPlainPropertyInputs()
            && MyGUI::TextBox::getCaption() == caption;
        if (!sameText)
            setCaption(caption);
        const int height = propertyValue("textSize", 10);
        const bool sameMetrics = sameText && getFontHeight() == height;
        if (!sameMetrics)
            setFontHeight(height);
        setTextColour(propertyValue("textColor", MyGUI::Colour(0, 0, 0, 1)));
        const bool multiline = propertyValue("multiline", false);
        const bool sameLines = sameMetrics && multiline == getEditMultiLine();
        if (!sameLines)
            setEditMultiLine(multiline);
        const bool wrap = propertyValue("wordWrap", false);
        if (!sameLines || wrap != getEditWordWrap())
            setEditWordWrap(wrap);

        Alignment horizontal(propertyValue("textAlignH", Alignment::Start));
        Alignment vertical(propertyValue("textAlignV", Alignment::Start));
        setTextAlign(alignmentToMyGui(horizontal, vertical));

        setTextShadow(propertyValue("textShadow", false));
        setTextShadowColour(propertyValue("textShadowColor", MyGUI::Colour(0, 0, 0, 1)));

        WidgetExtension::updateProperties();
    }

    void LuaText::setCaption(const MyGUI::UString& caption)
    {
        invalidateLayout();
        MyGUI::TextBox::setCaption(caption);
        if (mAutoSized)
            updateCoord();
    }

    const std::vector<std::string_view>& LuaText::allUsedProperties() const
    {
        static std::vector<std::string_view> usedProps = std::invoke([this] {
            std::vector<std::string_view> props = { "text", "textSize", "textColor", "wordWrap", "textAlignH",
                "textAlignV", "multiline", "readOnly", "autoSize", "textShadow", "textShadowColor" };
            auto baseProps = WidgetExtension::allUsedProperties();
            props.insert(props.end(), baseProps.begin(), baseProps.end());
            return props;
        });
        return usedProps;
    }

    MyGUI::IntSize LuaText::calculateSize() const
    {
        if (mAutoSized)
            return getTextSize();
        else
            return WidgetExtension::calculateSize();
    }

    MyGUI::IntCoord LuaText::getTextRegion() const
    {
        const MyGUI::IntPoint offset = WidgetExtension::getContentOffset();
        const MyGUI::IntSize size = WidgetExtension::getContentSize();
        return MyGUI::IntCoord(offset, size);
    }

    MyGUI::IntSize LuaText::getViewSize() const
    {
        return WidgetExtension::getContentSize();
    }
}
