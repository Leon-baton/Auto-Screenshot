#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include <windows.h>
#include <WinUser.h>
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/GManager.hpp>
#include <Geode/cocos/cocoa/CCObject.h>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <cocos2d.h>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

struct AutoScreenshotLevel
{
    bool screenshotOnNewBest = true;
    bool screenshotOnComplete = true;
    int id = 0;
    short percent = 50;

    AutoScreenshotLevel(bool b, bool c, int e, short f)
        : screenshotOnNewBest(b), screenshotOnComplete(c), id(e), percent(f) {}
    AutoScreenshotLevel() = default;

    void serialize(std::ofstream &file)
    {
        file.write(reinterpret_cast<char *>(&screenshotOnNewBest), sizeof(bool));
        file.write(reinterpret_cast<char *>(&screenshotOnComplete), sizeof(bool));
        file.write(reinterpret_cast<char *>(&id), sizeof(int));
        file.write(reinterpret_cast<char *>(&percent), sizeof(short));
    }

    void deserialize(std::ifstream &file)
    {
        file.read(reinterpret_cast<char *>(&screenshotOnNewBest), sizeof(bool));
        file.read(reinterpret_cast<char *>(&screenshotOnComplete), sizeof(bool));
        file.read(reinterpret_cast<char *>(&id), sizeof(int));
        file.read(reinterpret_cast<char *>(&percent), sizeof(short));
    }
};

AutoScreenshotLevel currentlyLoadedLevel;
std::vector<AutoScreenshotLevel> loadedAutoScreenshotLevels;
std::vector<uint32_t> screenshotKeybind = {VK_F12};

std::string getSettingsPath()
{
    auto dir = Mod::get()->getSaveDir();
    return dir.string() + "/autoscreenshot_settings.dat";
}

void saveScreenshotSettings()
{
    std::string path = getSettingsPath();
    log::info("Saving settings to {}", path);

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        log::error("Failed to open file for writing: {}", path);
        return;
    }

    bool const &defNewBest = Mod::get()->getSettingValue<bool>("Screenshot on new best (Default)");
    bool const &defComplete = Mod::get()->getSettingValue<bool>("Screenshot on complete (Default)");
    short const &defPercent = static_cast<short>(Mod::get()->getSettingValue<int64_t>("Screenshot after N percent (Default)") & 0xFFFF);

    std::vector<AutoScreenshotLevel> toSave;
    for (const auto &level : loadedAutoScreenshotLevels)
    {
        if (level.screenshotOnNewBest != defNewBest ||
            level.screenshotOnComplete != defComplete ||
            level.percent != defPercent)
        {
            toSave.push_back(level);
        }
    }

    int version = 1;
    file.write(reinterpret_cast<char *>(&version), sizeof(int));

    int count = toSave.size();
    file.write(reinterpret_cast<char *>(&count), sizeof(int));

    for (auto &level : toSave)
    {
        level.serialize(file);
    }

    log::info("Successfully saved {} level settings (filtered)", count);
}

void loadFile()
{
    std::string path = getSettingsPath();
    log::info("Loading settings from {}", path);

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        log::warn("No settings file found at {}, using defaults", path);
        return;
    }

    int version;
    file.read(reinterpret_cast<char *>(&version), sizeof(int));
    if (version != 1)
    {
        log::error("Unknown settings version {}", version);
        return;
    }

    int count;
    file.read(reinterpret_cast<char *>(&count), sizeof(int));
    log::info("Loading {} level settings", count);

    loadedAutoScreenshotLevels.clear();
    for (int i = 0; i < count; i++)
    {
        AutoScreenshotLevel level;
        level.deserialize(file);
        loadedAutoScreenshotLevels.push_back(level);
    }

    log::info("Successfully loaded settings for {} levels", loadedAutoScreenshotLevels.size());
}
void sendKeyEvent(uint32_t key, int state)
{
    INPUT inputs[1] = {};
    inputs[0].type = INPUT_KEYBOARD;

    if (key == 163 || key == 165)
    {
        inputs[0].ki.dwFlags = state | KEYEVENTF_EXTENDEDKEY;
    }
    else
    {
        inputs[0].ki.dwFlags = state;
    }

    inputs[0].ki.wScan = 0;
    inputs[0].ki.wVk = key;
    SendInput(1, inputs, sizeof(INPUT));
}

void takeScreenshot()
{
    for (auto key : screenshotKeybind)
    {
        sendKeyEvent(key, 0);
    }
    for (auto key : screenshotKeybind)
    {
        sendKeyEvent(key, 2);
    }
    log::info("Screenshot triggered successfully");
}

void saveLevel(AutoScreenshotLevel lvl)
{
    bool const &screenshotOnNewBestDefault = Mod::get()->getSettingValue<bool>("Screenshot on new best (Default)");
    bool const &screenshotOnCompleteDefault = Mod::get()->getSettingValue<bool>("Screenshot on complete (Default)");
    short const &defaultPercent = static_cast<short>(Mod::get()->getSettingValue<int64_t>("Screenshot after N percent (Default)") & 0xFFFF);

    bool isDefaultSettings =
        (lvl.screenshotOnNewBest == screenshotOnNewBestDefault) &&
        (lvl.screenshotOnComplete == screenshotOnCompleteDefault) &&
        (lvl.percent == defaultPercent);

    bool levelExists = false;

    for (auto &level : loadedAutoScreenshotLevels)
    {
        if (level.id == lvl.id)
        {
            levelExists = true;
            if (isDefaultSettings)
            {
                auto oldSize = loadedAutoScreenshotLevels.size();
                loadedAutoScreenshotLevels.erase(
                    std::remove_if(loadedAutoScreenshotLevels.begin(),
                                   loadedAutoScreenshotLevels.end(),
                                   [&](const AutoScreenshotLevel &x)
                                   { return x.id == lvl.id; }),
                    loadedAutoScreenshotLevels.end());
                if (oldSize != loadedAutoScreenshotLevels.size())
                {
                    log::debug("Settings for level {} are default - removed from save", lvl.id);
                }
            }
            else if (level.screenshotOnNewBest != lvl.screenshotOnNewBest ||
                     level.screenshotOnComplete != lvl.screenshotOnComplete ||
                     level.percent != lvl.percent)
            {
                level.screenshotOnNewBest = lvl.screenshotOnNewBest;
                level.screenshotOnComplete = lvl.screenshotOnComplete;
                level.percent = lvl.percent;
                log::debug("Updated settings for level {}", lvl.id);
            }
            break;
        }
    }

    if (!levelExists && !isDefaultSettings)
    {
        loadedAutoScreenshotLevels.push_back(lvl);
        log::info("Added new settings for level {}", lvl.id);
    }
}
class $modify(LoadingLayer)
{
    bool init(bool p0)
    {
        if (!LoadingLayer::init(p0))
            return false;

        loadFile();
        return true;
    }
};

class $modify(GManager)
{
    void save()
    {
        GManager::save();
        saveScreenshotSettings();
    }
};

class $modify(PlayLayer)
{
    bool init(GJGameLevel *level, bool p1, bool p2)
    {
        if (!PlayLayer::init(level, p1, p2))
        {
            return false;
        }

        int id = m_level->m_levelID.value();

        bool found = false;
        for (AutoScreenshotLevel &lvl : loadedAutoScreenshotLevels)
        {
            if (lvl.id == id)
            {
                currentlyLoadedLevel = lvl;
                found = true;
                break;
            }
        }

        if (!found)
        {
            currentlyLoadedLevel = AutoScreenshotLevel(
                Mod::get()->getSettingValue<bool>("Screenshot on new best (Default)"),
                Mod::get()->getSettingValue<bool>("Screenshot on complete (Default)"),
                id,
                static_cast<short>(Mod::get()->getSettingValue<int64_t>("Screenshot after N percent (Default)") & 0xFFFF));
            loadedAutoScreenshotLevels.push_back(currentlyLoadedLevel);
        }

        return true;
    }

    void onQuit()
    {
        PlayLayer::onQuit();
        saveLevel(currentlyLoadedLevel);
        currentlyLoadedLevel = AutoScreenshotLevel();
    }

    void showNewBest(bool p0, int p1, int p2, bool p3, bool p4, bool p5)
    {
        PlayLayer::showNewBest(p0, p1, p2, p3, p4, p5);

        int currentPercent = this->getCurrentPercentInt();

        if (currentlyLoadedLevel.screenshotOnNewBest && currentPercent >= currentlyLoadedLevel.percent)
        {
            log::info("Sending screenshot keybind...");
            auto delayValue = Mod::get()->getSettingValue<float>("Delay");
            this->runAction(
                CCSequence::create(
                    CCDelayTime::create(delayValue),
                    CallFuncExt::create([]
                                        { takeScreenshot(); }),
                    nullptr));

            log::info("Screenshot triggered successfully");
        }
    }

    void levelComplete()
    {
        PlayLayer::levelComplete();

        if (currentlyLoadedLevel.screenshotOnComplete &&
            this->getCurrentPercentInt() >= 100)
        {
            log::info("Level 100% completed - triggering screenshot");
            auto delayValue = Mod::get()->getSettingValue<float>("Delay");
            this->runAction(
                CCSequence::create(
                    CCDelayTime::create(delayValue),
                    CallFuncExt::create([]
                                        { takeScreenshot(); }),
                    nullptr));
        }
    }
    void resetLevel()
    {
        PlayLayer::resetLevel();
    }
};

CCMenuItemToggler *newBestButton;
CCMenuItemToggler *completeButton;

class ButtonLayer : public CCLayer
{
public:
    void toggleNewBest(CCObject *sender)
    {
        currentlyLoadedLevel.screenshotOnNewBest = !currentlyLoadedLevel.screenshotOnNewBest;
        newBestButton->toggle(!currentlyLoadedLevel.screenshotOnNewBest);
    };
    void toggleComplete(CCObject *sender)
    {
        currentlyLoadedLevel.screenshotOnComplete = !currentlyLoadedLevel.screenshotOnComplete;
        completeButton->toggle(!currentlyLoadedLevel.screenshotOnComplete);
    };
};

TextInput *percentInput;
bool currentlyInMenu = false;

class ConfigLayer : public geode::Popup<std::string const &>
{
protected:
    bool setup(std::string const &value) override
    {
        this->setKeyboardEnabled(true);
        currentlyInMenu = true;
        auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        const float offsetX = 50;
        const float buttonOffsetX = 190;
        CCPoint topLeftCorner = ccp(0, m_size.height);

        auto topLabel = CCLabelBMFont::create("AutoScreenshot", "goldFont.fnt");
        topLabel->setAnchorPoint({0.5, 0.5});
        topLabel->setScale(1.0f);
        topLabel->setPosition(topLeftCorner + ccp(150, 5));

        auto newBestLabel = CCLabelBMFont::create("On New Best", "bigFont.fnt");
        newBestLabel->setAnchorPoint({0, 0.5});
        newBestLabel->setScale(0.7f);
        newBestLabel->setPosition(topLeftCorner + ccp(offsetX, -60));

        newBestButton = CCMenuItemToggler::create(
            CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
            CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
            this,
            menu_selector(ButtonLayer::toggleNewBest));
        newBestButton->setPosition(newBestLabel->getPosition() + ccp(buttonOffsetX, 0));
        newBestButton->setScale(0.85f);
        newBestButton->toggle(currentlyLoadedLevel.screenshotOnNewBest);

        auto completeLabel = CCLabelBMFont::create("On Complete", "bigFont.fnt");
        completeLabel->setAnchorPoint({0, 0.5});
        completeLabel->setScale(0.7f);
        completeLabel->setPosition(topLeftCorner + ccp(offsetX, -100));

        completeButton = CCMenuItemToggler::create(
            CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
            CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
            this,
            menu_selector(ButtonLayer::toggleComplete));
        completeButton->setPosition(completeLabel->getPosition() + ccp(buttonOffsetX, 0));
        completeButton->setScale(0.85f);
        completeButton->toggle(currentlyLoadedLevel.screenshotOnComplete);

        auto percentLabel = CCLabelBMFont::create("After %", "bigFont.fnt");
        percentLabel->setAnchorPoint({0, 0.5});
        percentLabel->setScale(0.7f);
        percentLabel->setPosition(topLeftCorner + ccp(offsetX, -140));

        percentInput = TextInput::create(100.f, "%");
        percentInput->setFilter("0123456789");
        percentInput->setPosition(percentLabel->getPosition() + ccp(buttonOffsetX - 20, 0));
        percentInput->setScale(0.85f);
        percentInput->setMaxCharCount(3);
        percentInput->setString(std::to_string(currentlyLoadedLevel.percent));

        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        menu->addChild(newBestButton);
        menu->addChild(completeButton);
        menu->addChild(percentInput);

        m_mainLayer->addChild(topLabel);
        m_mainLayer->addChild(newBestLabel);
        m_mainLayer->addChild(completeLabel);
        m_mainLayer->addChild(percentLabel);
        m_mainLayer->addChild(menu);

        m_mainLayer->setContentSize({m_size.width, 220});
        return true;
    }

    void onClose(CCObject *a) override
    {
        Popup::onClose(a);
        if (percentInput != nullptr)
        {
            currentlyLoadedLevel.percent = std::clamp(stoi(percentInput->getString()), 1, 100);
        }
        currentlyInMenu = false;
    }

public:
    static ConfigLayer *create()
    {
        auto ret = new ConfigLayer();
        if (ret && ret->initAnchored(300, 200, "", "GJ_square04.png"))
        {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void openMenu(CCObject *)
    {
        auto layer = create();
        layer->show();
    }
};

class $modify(PauseLayer)
{
    void customSetup()
    {
        PauseLayer::customSetup();

        auto sprite = CCSprite::create("screenshotButton.png"_spr);
        sprite->setScale(0.24f);

        auto btn = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(ConfigLayer::openMenu));
        auto menu = this->getChildByID("right-button-menu");
        menu->addChild(btn, 100);
        menu->updateLayout();
    }
};
