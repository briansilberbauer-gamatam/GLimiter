#include "Version.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace
{

//==============================================================================
class GLimiterStandaloneWindow final : public juce::DocumentWindow
{
public:
    GLimiterStandaloneWindow (juce::AudioProcessor& audioProcessor,
                              juce::AudioDeviceManager& manager)
        : DocumentWindow ("GLimiter", juce::Colour (0xff07080b),
                          DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          processor (audioProcessor),
          deviceManager (manager)
    {
        setUsingNativeTitleBar (false);

        if (auto* editor = processor.createEditorIfNeeded())
            setContentNonOwned (editor, true);
        else
            setContentOwned (new juce::GenericAudioProcessorEditor (processor), true);

        setResizeLimits (885, 540, 1770, 1080);

        if (auto* boundsConstrainer = getConstrainer())
            boundsConstrainer->setFixedAspectRatio (1180.0 / 720.0);

        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());

        optionsButton.setButtonText ("AUDIO");
        optionsButton.setTooltip ("Open the audio and MIDI device settings");
        optionsButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d2026));
        optionsButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff929aa8));
        optionsButton.onClick = [this] { showAudioSettings(); };
        addAndMakeVisible (optionsButton);

        setVisible (true);
    }

    ~GLimiterStandaloneWindow() override
    {
        clearContentComponent();
    }

    void resized() override
    {
        DocumentWindow::resized();
        optionsButton.setBounds (8, 7, 54, juce::jmax (16, getTitleBarHeight() - 10));
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    void showAudioSettings()
    {
        juce::DialogWindow::LaunchOptions options;
        auto* selector = new juce::AudioDeviceSelectorComponent (
            deviceManager, 0, 0, 2, 2, false, false, true, false);
        selector->setSize (520, 440);

        options.content.setOwned (selector);
        options.dialogTitle = "GLimiter - Audio / MIDI Settings";
        options.dialogBackgroundColour = juce::Colour (0xff0d0f13);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.componentToCentreAround = this;
        options.launchAsync();
    }

    juce::AudioProcessor& processor;
    juce::AudioDeviceManager& deviceManager;
    juce::TextButton optionsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GLimiterStandaloneWindow)
};

//==============================================================================
class GLimiterStandaloneApp final : public juce::JUCEApplication
{
public:
    GLimiterStandaloneApp() = default;

    const juce::String getApplicationName() override    { return "GLimiter"; }
    const juce::String getApplicationVersion() override { return glimiter::version; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        processor.reset (createPluginFilter());
        deviceManager = std::make_unique<juce::AudioDeviceManager>();

        // Show the unit before touching the audio backend so a misbehaving
        // driver can never leave the user staring at an empty desktop.
        mainWindow = std::make_unique<GLimiterStandaloneWindow> (*processor, *deviceManager);
        startAudio();
    }

    void shutdown() override
    {
        stopAudio();
        mainWindow.reset();
        deviceManager.reset();
        processor.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    void startAudio()
    {
        if (processor == nullptr || deviceManager == nullptr)
            return;

        player.setProcessor (processor.get());

        // Register before opening a device so the selector can also route audio
        // correctly if the user visits the AUDIO page after a failed startup.
        deviceManager->addAudioCallback (&player);

       #if JUCE_LINUX
        // Without /dev/snd the ALSA PipeWire plugin can block forever inside
        // snd_pcm_prepare(). Skip auto-opening in that case; the AUDIO button
        // still exposes the full device selector.
        if (! juce::File ("/dev/snd").isDirectory())
            return;

        // Prefer direct ALSA hardware over the PipeWire/Pulse PCM plugins,
        // which can also stall while a session is being (re)connected.
        for (auto* type : deviceManager->getAvailableDeviceTypes())
        {
            if (type->getTypeName().containsIgnoreCase ("hw"))
            {
                deviceManager->setCurrentAudioDeviceType (type->getTypeName(), false);
                break;
            }
        }
       #endif

        juce::ignoreUnused (deviceManager->initialiseWithDefaultDevices (0, 2));
    }

    void stopAudio()
    {
        if (deviceManager != nullptr)
            deviceManager->removeAudioCallback (&player);

        player.setProcessor (nullptr);
    }

    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<juce::AudioDeviceManager> deviceManager;
    std::unique_ptr<GLimiterStandaloneWindow> mainWindow;
    juce::AudioProcessorPlayer player;
};

} // namespace

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new GLimiterStandaloneApp();
}
