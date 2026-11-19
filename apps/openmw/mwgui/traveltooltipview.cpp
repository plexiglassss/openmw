#include "traveltooltipview.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ITexture.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextIterator.h>
#include <MyGUI_Widget.h>

namespace MWGui
{
    namespace
    {
        constexpr std::array<std::string_view, 3> fonts = { "DefaultFont", "MonoFont", "ScrollFont" };

        const std::array<MyGUI::Colour, 11> colours = { MyGUI::Colour(1.f, 1.f, 1.f),
            MyGUI::Colour(.9f, .8f, .5f), MyGUI::Colour(.5f, 0.f, 0.f), MyGUI::Colour(0.f, .7f, .7f),
            MyGUI::Colour(.3f, .2f, .5f), MyGUI::Colour(.5f, .3f, 0.f), MyGUI::Colour(0.f, .3f, .1f),
            MyGUI::Colour(.4f, .1f, .2f), MyGUI::Colour(1.f, .9f, .7f), MyGUI::Colour(0.f, .3f, .8f),
            MyGUI::Colour(1.f, .4f, .7f) };

        MyGUI::EditBox* createText(MyGUI::Widget* parent, std::string_view name)
        {
            auto* text = parent->createWidget<MyGUI::EditBox>("SandText", MyGUI::IntCoord(), MyGUI::Align::Default, name);
            text->setEditStatic(true);
            text->setEditMultiLine(true);
            text->setEditWordWrap(true);
            text->setNeedMouseFocus(false);
            text->setNeedKeyFocus(false);
            return text;
        }
    }

    TravelTooltipView::TravelTooltipView(MyGUI::Widget* parent)
    {
        mPanel = parent->createWidget<MyGUI::Widget>("HUD_Box_NoTransp", MyGUI::IntCoord(), MyGUI::Align::Default, "TravelTooltipPanel");
        mPanel->setNeedMouseFocus(false);
        mPanel->setNeedKeyFocus(false);
        mTitle = createText(mPanel, "TravelTooltipTitle");
        mDescription = createText(mPanel, "TravelTooltipDescription");
        mImage = mPanel->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(), MyGUI::Align::Default, "TravelTooltipImage");
        mImage->setNeedMouseFocus(false);
        mImage->setNeedKeyFocus(false);
        mPanel->setVisible(false);
    }

    TravelTooltipView::~TravelTooltipView()
    {
        // The view is destroyed before its parent widgets and before MyGUI shuts down.
        // Remove the widget's render reference before releasing the owned texture.
        if (mTexture)
        {
            mImage->setImageTexture("");
            MyGUI::RenderManager::getInstance().destroyTexture(mTexture);
        }
    }

    MyGUI::IntSize TravelTooltipView::render(const TravelTooltips::Image& image, std::string_view title,
        std::string_view description, bool overview, const TravelTooltipStyle& style)
    {
        if (image.mWidth <= 0 || image.mHeight <= 0 || style.mViewWidth < 80 || style.mViewHeight < 80)
            throw std::runtime_error("invalid image or viewport dimensions for travel preview");
        MyGUI::IntSize size;
        const float scale = std::clamp(style.mScale, 50, 200) / 100.f;
        const int padding = std::max(6, static_cast<int>(std::lround(8 * scale)));
        const int gap = std::max(3, static_cast<int>(std::lround(5 * scale)));
        const int maxWidth = std::max(1, style.mViewWidth - 16);
        const int maxHeight = std::max(1, style.mViewHeight - 16);

        if (mLoadedImage != image.mPath)
        {
            if (!mTexture)
                mTexture = MyGUI::RenderManager::getInstance().createTexture(
                    "OpenMW_TravelTooltip_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));

            // A path passed directly to setImageTexture is cached by the GUI renderer
            // for its entire lifetime. Reuse one owned texture for the large art pack.
            mImage->setImageTexture("");
            mLoadedImage.clear();
            mTexture->destroy();
            mTexture->loadFromFile(image.mPath);
            mImage->setImageTexture(mTexture->getName());
            mLoadedImage = image.mPath;
        }
        mImage->setColour(colours[std::clamp(style.mColour, 0, 10)]);
        mTitle->setVisible(!overview);
        mDescription->setVisible(!overview);

        if (overview)
        {
            const float fit = std::min({ scale, static_cast<float>(maxWidth - 2 * padding) / image.mWidth,
                static_cast<float>(maxHeight - 2 * padding) / image.mHeight });
            const int width = std::max(1, static_cast<int>(image.mWidth * fit));
            const int height = std::max(1, static_cast<int>(image.mHeight * fit));
            mImage->setCoord(padding, padding, width, height);
            size = MyGUI::IntSize(width + 2 * padding, height + 2 * padding);
        }
        else
        {
            int width = std::min(maxWidth, static_cast<int>((style.mWide ? 900 : 500) * scale));
            int innerWidth = std::max(1, width - 2 * padding);
            mTitle->setFontName(fonts[std::clamp(style.mTitleFont, 0, 2)]);
            mDescription->setFontName(fonts[std::clamp(style.mTextFont, 0, 2)]);
            mTitle->setTextAlign(MyGUI::Align::HCenter | MyGUI::Align::Top);
            mDescription->setTextAlign((style.mWide ? MyGUI::Align::HCenter : MyGUI::Align::Left) | MyGUI::Align::Top);
            mTitle->setCaption(MyGUI::TextIterator::toTagsString(std::string(title)));
            mDescription->setCaption(MyGUI::TextIterator::toTagsString(std::string(description)));
            mTitle->setCoord(padding, padding, innerWidth, maxHeight);
            mDescription->setCoord(padding, padding, innerWidth, maxHeight);

            // Keep the complete description readable when the viewport/UI scaling leaves
            // less room than the requested size. Shrink text only as far as necessary.
            int fontHeight = std::clamp(static_cast<int>(std::lround(style.mFontSize * scale)), 9, 64);
            int titleHeight = 0;
            int textHeight = 0;
            do
            {
                mTitle->setFontHeight(fontHeight + 2);
                mDescription->setFontHeight(fontHeight);
                titleHeight = mTitle->getTextSize().height;
                textHeight = mDescription->getCaption().empty() ? 0 : mDescription->getTextSize().height;
                if (titleHeight + textHeight + 2 * padding + 2 * gap + 80 <= maxHeight)
                    break;
                if (fontHeight > 9)
                    --fontHeight;
                else if (width < maxWidth)
                {
                    // A long description can need more width than the chosen slim layout.
                    // Use the available viewport before sacrificing further readability.
                    width = maxWidth;
                    innerWidth = std::max(1, width - 2 * padding);
                    mTitle->setSize(innerWidth, maxHeight);
                    mDescription->setSize(innerWidth, maxHeight);
                }
                else
                    break;
            } while (true);

            const int imageSpace = maxHeight - titleHeight - textHeight - 2 * padding - 2 * gap;
            if (imageSpace < 1)
                throw std::runtime_error("viewport is too small for the complete travel description; reduce UI scale");
            const float fit = std::min({ .77f * scale, static_cast<float>(innerWidth) / image.mWidth,
                static_cast<float>(imageSpace) / image.mHeight });
            const int imageHeight = std::max(1, static_cast<int>(image.mHeight * fit));
            const int imageWidth = std::max(1, static_cast<int>(image.mWidth * fit));
            mTitle->setCoord(padding, padding, innerWidth, titleHeight);
            const int imageTop = padding + titleHeight + gap;
            mImage->setCoord((width - imageWidth) / 2, imageTop, imageWidth, imageHeight);
            const int textTop = imageTop + imageHeight + (textHeight > 0 ? gap : 0);
            mDescription->setCoord(padding, textTop, innerWidth, textHeight);
            size = MyGUI::IntSize(width, textTop + textHeight + padding);
        }
        mPanel->setCoord(0, 0, size.width, size.height);
        return size;
    }

    void TravelTooltipView::setVisible(bool visible)
    {
        mPanel->setVisible(visible);
    }
}
