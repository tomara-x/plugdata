/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

class MainMenu : public PopupMenu {

public:
    MainMenu(PluginEditor* editor)
        : settingsTree(SettingsFile::getInstance()->getValueTree())
        , themeSelector(settingsTree)
        , zoomSelector(settingsTree)
    {
        addCustomItem(1, themeSelector, 70, 45, false);
        addCustomItem(2, zoomSelector, 70, 30, false);
        addSeparator();

        addCustomItem(1, std::unique_ptr<IconMenuItem>(menuItems[0]), nullptr, "New patch");

        addSeparator();

        addCustomItem(2, std::unique_ptr<IconMenuItem>(menuItems[1]), nullptr, "Open patch");

        auto recentlyOpened = new PopupMenu();

        auto recentlyOpenedTree = settingsTree.getChildWithName("RecentlyOpened");
        if (recentlyOpenedTree.isValid()) {
            for (int i = 0; i < recentlyOpenedTree.getNumChildren(); i++) {
                auto path = File(recentlyOpenedTree.getChild(i).getProperty("Path").toString());
                recentlyOpened->addItem(path.getFileName(), [this, path, editor]() mutable {
                    editor->pd->loadPatch(path);
                    SettingsFile::getInstance()->addToRecentlyOpened(path);
                });
            }

            menuItems[2]->isActive = recentlyOpenedTree.getNumChildren() > 0;
        }

        addCustomItem(100, std::unique_ptr<IconMenuItem>(menuItems[2]), std::unique_ptr<const PopupMenu>(recentlyOpened), "Recently opened");

        addSeparator();
        addCustomItem(3, std::unique_ptr<IconMenuItem>(menuItems[3]), nullptr, "Save patch");
        addCustomItem(4, std::unique_ptr<IconMenuItem>(menuItems[4]), nullptr, "Save patch as");

        addSeparator();

        addCustomItem(5, std::unique_ptr<IconMenuItem>(menuItems[5]), nullptr, "Compiled mode");
        addCustomItem(6, std::unique_ptr<IconMenuItem>(menuItems[6]), nullptr, "Compile...");

        addSeparator();

        addCustomItem(7, std::unique_ptr<IconMenuItem>(menuItems[7]), nullptr, "Auto-connect objects");

        addSeparator();

        addCustomItem(8, std::unique_ptr<IconMenuItem>(menuItems[8]), nullptr, "Settings...");
        addCustomItem(9, std::unique_ptr<IconMenuItem>(menuItems[9]), nullptr, "About...");

        // Toggles hvcc compatibility mode
        bool hvccModeEnabled = settingsTree.hasProperty("HvccMode") ? static_cast<bool>(settingsTree.getProperty("HvccMode")) : false;
        bool autoconnectEnabled = settingsTree.hasProperty("AutoConnect") ? static_cast<bool>(settingsTree.getProperty("AutoConnect")) : false;
        bool hasCanvas = editor->getCurrentCanvas() != nullptr;
        ;

        menuItems[3]->isActive = hasCanvas;
        menuItems[4]->isActive = hasCanvas;

        menuItems[5]->isTicked = hvccModeEnabled;
        menuItems[7]->isTicked = autoconnectEnabled;
    }

    class ZoomSelector : public Component {
        TextButton zoomIn;
        TextButton zoomOut;
        TextButton zoomReset;

        Value zoomValue;

    public:
        ZoomSelector(ValueTree settingsTree)
        {
            zoomValue = settingsTree.getPropertyAsValue("Zoom", nullptr);

            zoomIn.setButtonText("+");
            zoomReset.setButtonText(String(static_cast<float>(zoomValue.getValue()) * 100, 1) + "%");
            zoomOut.setButtonText("-");

            addAndMakeVisible(zoomIn);
            addAndMakeVisible(zoomReset);
            addAndMakeVisible(zoomOut);

            zoomIn.setConnectedEdges(Button::ConnectedOnLeft);
            zoomOut.setConnectedEdges(Button::ConnectedOnRight);
            zoomReset.setConnectedEdges(12);

            zoomIn.onClick = [this]() {
                applyZoom(true);
            };
            zoomOut.onClick = [this]() {
                applyZoom(false);
            };
            zoomReset.onClick = [this]() {
                resetZoom();
            };
        }

        void applyZoom(bool zoomIn)
        {
            float value = static_cast<float>(zoomValue.getValue());

            // Apply limits
            value = std::clamp(zoomIn ? value + 0.1f : value - 0.1f, 0.5f, 2.0f);

            // Round in case we zoomed with scrolling
            value = static_cast<float>(static_cast<int>(round(value * 10.))) / 10.;

            zoomValue = value;

            zoomReset.setButtonText(String(value * 100.0f, 1) + "%");
        }

        void resetZoom()
        {
            zoomValue = 1.0f;
            zoomReset.setButtonText("100.0%");
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(8, 4);
            int buttonWidth = (getWidth() - 8) / 3;

            zoomOut.setBounds(bounds.removeFromLeft(buttonWidth).expanded(1, 0));
            zoomReset.setBounds(bounds.removeFromLeft(buttonWidth).expanded(1, 0));
            zoomIn.setBounds(bounds.removeFromLeft(buttonWidth).expanded(1, 0));
        }
    };

    class IconMenuItem : public PopupMenu::CustomComponent {

        String menuItemIcon;
        String menuItemText;

        bool hasSubMenu;
        bool hasTickBox;
        bool isMouseOver = false;

    public:
        bool isTicked = false;
        bool isActive = true;

        IconMenuItem(String icon, String text, bool hasChildren, bool tickBox)
            : menuItemIcon(icon)
            , menuItemText(text)
            , hasSubMenu(hasChildren)
            , hasTickBox(tickBox)
        {
        }

        void getIdealSize(int& idealWidth, int& idealHeight) override
        {
            idealWidth = 70;
            idealHeight = 22;
        }

        void paint(Graphics& g) override
        {
            auto r = getLocalBounds().reduced(0, 1);

            auto colour = findColour(PopupMenu::textColourId).withMultipliedAlpha(isActive ? 1.0f : 0.5f);
            if (isItemHighlighted() && isActive) {
                g.setColour(findColour(PlugDataColour::popupMenuActiveBackgroundColourId));
                g.fillRoundedRectangle(r.toFloat().reduced(2, 0), 4.0f);

                colour = findColour(PlugDataColour::popupMenuActiveTextColourId);
            }

            g.setColour(colour);

            r.reduce(jmin(5, r.getWidth() / 20), 0);

            auto maxFontHeight = (float)r.getHeight() / 1.3f;

            auto& lnf = dynamic_cast<PlugDataLook&>(getLookAndFeel());
            auto iconArea = r.removeFromLeft(roundToInt(maxFontHeight)).withSizeKeepingCentre(maxFontHeight, maxFontHeight);

            if (menuItemIcon.isNotEmpty()) {
                g.setFont(lnf.iconFont.withHeight(std::min(15.0f, maxFontHeight)));

                PlugDataLook::drawFittedText(g, menuItemIcon, iconArea, Justification::centredLeft, colour);
            } else if (hasTickBox) {

                g.setColour(findColour(ToggleButton::tickDisabledColourId));
                g.drawRoundedRectangle(iconArea.toFloat().translated(0, 0.5f), 4.0f, 1.0f);

                if (isTicked) {
                    g.setColour(findColour(ToggleButton::tickColourId));
                    auto tick = lnf.getTickShape(1.0f);
                    g.fillPath(tick, tick.getTransformToScaleToFit(iconArea.toFloat().translated(0, 0.5f).reduced(2.5f, 3.5f), false));
                }

                /*
                auto tick = lnf.getTickShape(1.0f);
                g.fillPath(tick, tick.getTransformToScaleToFit(, true)); */
            }

            r.removeFromLeft(roundToInt(maxFontHeight * 0.5f));

            auto font = Font(std::min(17.0f, maxFontHeight));

            g.setFont(font);

            if (hasSubMenu) {
                auto arrowH = 0.6f * font.getAscent();

                auto x = static_cast<float>(r.removeFromRight((int)arrowH).getX());
                auto halfH = static_cast<float>(r.getCentreY());

                Path path;
                path.startNewSubPath(x, halfH - arrowH * 0.5f);
                path.lineTo(x + arrowH * 0.6f, halfH);
                path.lineTo(x, halfH + arrowH * 0.5f);

                g.strokePath(path, PathStrokeType(2.0f));
            }

            r.removeFromRight(3);
            PlugDataLook::drawFittedText(g, menuItemText, r, Justification::centredLeft, colour);

            /*
            if (shortcutKeyText.isNotEmpty()) {
                auto f2 = font;
                f2.setHeight(f2.getHeight() * 0.75f);
                f2.setHorizontalScale(0.95f);
                g.setFont(f2);

             PlugDataLook::drawText(g, shortcutKeyText, r.translated(-2, 0), Justification::centredRight, findColour(PopupMenu::textColourId));
            } */
        }
    };

    class ThemeSelector : public Component {

        Value theme;
        ValueTree settingsTree;

    public:
        ThemeSelector(ValueTree tree)
            : settingsTree(tree)
        {
            theme.referTo(settingsTree.getPropertyAsValue("Theme", nullptr));
        }

        void paint(Graphics& g)
        {
            auto secondBounds = getLocalBounds();
            auto firstBounds = secondBounds.removeFromLeft(getWidth() / 2.0f);

            firstBounds = firstBounds.withSizeKeepingCentre(30, 30);
            secondBounds = secondBounds.withSizeKeepingCentre(30, 30);

            auto themesTree = settingsTree.getChildWithName("ColourThemes");
            auto firstThemeTree = themesTree.getChildWithProperty("theme", PlugDataLook::selectedThemes[0]);
            auto secondThemeTree = themesTree.getChildWithProperty("theme", PlugDataLook::selectedThemes[1]);

            g.setColour(PlugDataLook::getThemeColour(firstThemeTree, PlugDataColour::canvasBackgroundColourId));
            g.fillEllipse(firstBounds.toFloat());

            g.setColour(PlugDataLook::getThemeColour(secondThemeTree, PlugDataColour::canvasBackgroundColourId));
            g.fillEllipse(secondBounds.toFloat());

            g.setColour(PlugDataLook::getThemeColour(firstThemeTree, PlugDataColour::objectOutlineColourId));
            g.drawEllipse(firstBounds.toFloat(), 1.0f);

            g.setColour(PlugDataLook::getThemeColour(secondThemeTree, PlugDataColour::objectOutlineColourId));
            g.drawEllipse(secondBounds.toFloat(), 1.0f);

            auto tick = getLookAndFeel().getTickShape(0.6f);
            auto tickBounds = Rectangle<int>();

            if (theme.toString() == firstThemeTree.getProperty("theme").toString()) {
                g.setColour(PlugDataLook::getThemeColour(firstThemeTree, PlugDataColour::canvasTextColourId));
                tickBounds = firstBounds;
            } else {
                g.setColour(PlugDataLook::getThemeColour(secondThemeTree, PlugDataColour::canvasTextColourId));
                tickBounds = secondBounds;
            }

            g.fillPath(tick, tick.getTransformToScaleToFit(tickBounds.reduced(9, 9).toFloat(), false));
        }

        void mouseUp(MouseEvent const& e)
        {
            auto secondBounds = getLocalBounds();
            auto firstBounds = secondBounds.removeFromLeft(getWidth() / 2.0f);

            firstBounds = firstBounds.withSizeKeepingCentre(30, 30);
            secondBounds = secondBounds.withSizeKeepingCentre(30, 30);

            if (firstBounds.contains(e.x, e.y)) {
                theme = PlugDataLook::selectedThemes[0];
                repaint();
            } else if (secondBounds.contains(e.x, e.y)) {
                theme = PlugDataLook::selectedThemes[1];
                repaint();
            }
        }
    };

    std::vector<IconMenuItem*> menuItems = {
        new IconMenuItem(Icons::New, "New patch", false, false),
        new IconMenuItem(Icons::Open, "Open patch...", false, false),
        new IconMenuItem(Icons::History, "Recently opened", true, false),

        new IconMenuItem(Icons::Save, "Save patch", false, false),
        new IconMenuItem(Icons::SaveAs, "Save patch as...", false, false),

        new IconMenuItem("", "Compiled Mode", false, true),
        new IconMenuItem("", "Compile...", false, false),

        new IconMenuItem("", "Auto-connect objects", false, true),

        new IconMenuItem(Icons::Settings, "Settings...", false, false),
        new IconMenuItem(Icons::Info, "About...", false, false),
    };

    ValueTree settingsTree;
    ThemeSelector themeSelector;
    ZoomSelector zoomSelector;


};
