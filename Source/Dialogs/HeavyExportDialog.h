/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include "Canvas.h"

#if JUCE_LINUX
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

struct ExporterSettingsPanel : public Component, public Value::Listener, public Timer, public ChildProcess, public Thread
{
    inline static File toolchain = File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getChildFile("PlugData").getChildFile("Toolchain");
    
    TextButton exportButton = TextButton("Export");
    
    ComboBox patchChooser;
    
    TextEditor outputPathEditor;
    TextButton outputPathBrowseButton = TextButton("Browse");
    
    TextEditor projectNameEditor;
    TextEditor projectCopyrightEditor;
    
    File patchFile;
    File openedPatchFile;
    
    int labelWidth = 180;
    
    ExporterSettingsPanel(PlugDataPluginEditor* editor) : Thread ("Heavy Export Thread"){
        addAndMakeVisible(exportButton);
        addAndMakeVisible(patchChooser);
        addAndMakeVisible(outputPathEditor);
        addAndMakeVisible(outputPathBrowseButton);
        
        addAndMakeVisible(projectNameEditor);
        addAndMakeVisible(projectCopyrightEditor);
        
        outputPathBrowseButton.setConnectedEdges(12);
        
        patchChooser.addItem("Currently opened patch", 1);
        patchChooser.addItem("Other patch (browse)", 2);
        
        exporterSelected.addListener(this);
        validPatchSelected.addListener(this);
        targetFolderSelected.addListener(this);
        
        if(auto* cnv = editor->getCurrentCanvas())
        {
            // TODO: because we save it to a tempfile, it's important to add the real path (if applicable) to Heavy's search paths!
            openedPatchFile = File::createTempFile(".pd");
            openedPatchFile.replaceWithText(cnv->patch.getCanvasContent());
            patchChooser.setItemEnabled(1, true);
            patchChooser.setSelectedId(1);
            patchFile = openedPatchFile;
        }
        else {
            patchChooser.setItemEnabled(1, false);
            patchChooser.setSelectedId(2);
            validPatchSelected = false;
        }
        
        patchChooser.onChange = [this](){
            if(patchChooser.getSelectedId() == 1) {
                patchFile = openedPatchFile;
                validPatchSelected = true;
            }
            else {
                // Open file browser
                openChooser = std::make_unique<FileChooser>("Choose file to open", File::getSpecialLocation(File::userHomeDirectory), "*.pd", true);
                
                openChooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](FileChooser const& fileChooser){
                    
                    auto result = fileChooser.getResult();
                    if(result.existsAsFile()) {
                        patchFile = result;
                        validPatchSelected = true;
                    }
                    else {
                        patchFile = "";
                        validPatchSelected = false;
                    }
                }
                                         );
            };
        };
        
        outputPathEditor.onTextChange = [this](){
            auto outputDir = File(outputPathEditor.getText());
            targetFolderSelected = outputDir.getParentDirectory().exists() && !outputDir.existsAsFile();
        };
        
        exportButton.onClick = [this, editor](){
            auto outDir = File(outputPathEditor.getText());
            outDir.createDirectory();
            startExport(patchFile.getFullPathName(), outDir.getFullPathName(), projectNameEditor.getText(), projectCopyrightEditor.getText(), {});
        };
        
        outputPathBrowseButton.onClick = [this](){
            auto constexpr folderChooserFlags = FileBrowserComponent::saveMode | FileBrowserComponent::canSelectDirectories | FileBrowserComponent::warnAboutOverwriting;
            
            saveChooser = std::make_unique<FileChooser>("Export directory", File::getSpecialLocation(File::userHomeDirectory), "", true);
            
            saveChooser->launchAsync(folderChooserFlags,
                                     [this](FileChooser const& fileChooser) {
                auto const file = fileChooser.getResult();
                outputPathEditor.setText(file.getFullPathName());
            });
        };
        
    }
    
    ~ExporterSettingsPanel() {
        if(openedPatchFile.existsAsFile()) {
            openedPatchFile.deleteFile();
        }
    }
    
    
    void valueChanged(Value& v) override
    {
        bool exportReady = static_cast<bool>(validPatchSelected.getValue()) && static_cast<bool>(targetFolderSelected.getValue());
        exportButton.setEnabled(exportReady);
    }
    
    void resized() override
    {
        auto b = Rectangle<int>(proportionOfWidth (0.1f), 0, proportionOfWidth (0.8f), getHeight());
        
        exportButton.setBounds(getLocalBounds().removeFromBottom(23).removeFromRight(80).translated(-10, -10));
        
        b.removeFromTop(20);
        
        patchChooser.setBounds(b.removeFromTop(23));
        
        b.removeFromTop(15);
        
        projectNameEditor.setBounds(b.removeFromTop(23).withTrimmedLeft(labelWidth));
        
        b.removeFromTop(15);
        
        projectCopyrightEditor.setBounds(b.removeFromTop(23).withTrimmedLeft(labelWidth));
        
        b.removeFromTop(15);
        
        auto outputPathBounds = b.removeFromTop(23);
        outputPathEditor.setBounds(outputPathBounds.removeFromLeft(proportionOfWidth (0.7f)).withTrimmedLeft(labelWidth));
        outputPathBrowseButton.setBounds(outputPathBounds.withTrimmedLeft(-1));
    }
    
    void paintOverChildren(Graphics& g) override
    {
        if(exportProgress != 0.0f)
        {
            auto* lnf = dynamic_cast<PlugDataLook*>(&getLookAndFeel());
            
            g.setColour(findColour(PlugDataColour::canvasBackgroundColourId));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
            
            g.setColour(findColour(PlugDataColour::canvasTextColourId));
            g.setFont(lnf->boldFont.withHeight(32));
            g.drawText("Exporting...", 0, getHeight() / 2 - 120, getWidth(), 40, Justification::centred);
            
            
            float width = getWidth() - 90.0f;
            float right = jmap(exportProgress, 90.f, width);
            
            Path downloadPath;
            downloadPath.addLineSegment({ 90, 300, right, 300 }, 1.0f);
            
            Path fullPath;
            fullPath.addLineSegment({ 90, 300, width, 300 }, 1.0f);
            
            g.setColour(findColour(PlugDataColour::panelTextColourId));
            g.strokePath(fullPath, PathStrokeType(11.0f, PathStrokeType::JointStyle::curved, PathStrokeType::EndCapStyle::rounded));
            
            g.setColour(findColour(PlugDataColour::panelActiveBackgroundColourId));
            g.strokePath(downloadPath, PathStrokeType(8.0f, PathStrokeType::JointStyle::curved, PathStrokeType::EndCapStyle::rounded));
        }
    }
    
    void paint(Graphics& g) override
    {
        auto b = Rectangle<int>(proportionOfWidth (0.1f), 60, labelWidth, getHeight() - 35);
        
        g.setFont(Font(15));
        g.setColour(findColour(PlugDataColour::panelTextColourId));
        g.drawText("Project Name (optional)", b.removeFromTop(23), Justification::centredLeft);
        
        b.removeFromTop(15);
        
        g.drawText("Project Copyright (optional)", b.removeFromTop(23), Justification::centredLeft);
        
        b.removeFromTop(15);
        
        g.drawText("Output Path", b.removeFromTop(23), Justification::centredLeft);
    }
    
private:
    
    void timerCallback() override {
        exportProgress = std::min(1.0f, exportProgress + 0.001f);
        repaint();
    }

    virtual void startExport(String pdPatch, String outdir, String name, String copyright, StringArray searchPaths) = 0;
    virtual void finishExport(String outputPath) = 0;
    
    void run() override {

        String output = readAllProcessOutput();
        
        finishExport(outputPathEditor.getText());
        
        MessageManager::callAsync([this](){
            stopTimer();
            exportProgress = 0.0f;
            repaint();
        });
    }
    
public:
    
    String createMetadata(String exporter, std::map<String, String> settings) {
        auto metadata = File::createTempFile(".json");
        
        String metaString = "{\n";
        metaString +=  "\"" + exporter + "\": {\n";
        for(auto& [key, value] : settings) {
            metaString += "\"" + key + "\": \"" + value + "\"\n";
        }
        metaString += "}\n";
        metaString += "}";
        
        metadata.replaceWithText(metaString);
        return metadata.getFullPathName();
    }
    
    float exportProgress = 0.0f;
    
    inline static File heavyExecutable = File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getChildFile("PlugData").getChildFile("Toolchain").getChildFile("bin").getChildFile("Heavy").getChildFile("Heavy");
    
    
    Value exporterSelected = Value(var(false));
    Value validPatchSelected = Value(var(false));
    Value targetFolderSelected = Value(var(false));
    
    
    std::unique_ptr<FileChooser> saveChooser;
    std::unique_ptr<FileChooser> openChooser;
};

class CppSettingsPanel : public ExporterSettingsPanel
{
public:
    CppSettingsPanel(PlugDataPluginEditor* editor) : ExporterSettingsPanel(editor) {
        
    }
    
    void startExport(String pdPatch, String outdir, String name, String copyright, StringArray searchPaths) override
    {
       StringArray args = {heavyExecutable.getFullPathName(), pdPatch, "-o" + outdir};
       
       if(name.isNotEmpty()) {
           args.add("-n" + name);
       }
       else {
           args.add("-nUntitled");
       }
       
       if(copyright.isNotEmpty()) {
           args.add("--copyright");
           args.add("\"" + name + "\"");
       }
    
       start(args);
       startThread(3);
       startTimer(10);
   }
    
    void finishExport(String outPath) override
    {
        auto outputFile = File(outPath);
        outputFile.getChildFile("ir").deleteRecursively();
        outputFile.getChildFile("hv").deleteRecursively();
    }
};

class DaisySettingsPanel : public ExporterSettingsPanel
{
public:
    
    ComboBox targetBoard;
    
    DaisySettingsPanel(PlugDataPluginEditor* editor) : ExporterSettingsPanel(editor) {
        
        targetBoard.addItem("Seed", 1);
        targetBoard.addItem("Pod", 2);
        targetBoard.addItem("Petal", 3);
        targetBoard.addItem("Patch", 4);

        targetBoard.setSelectedId(1);
        
        addAndMakeVisible(targetBoard);
    }

    void startExport(String pdPatch, String outdir, String name, String copyright, StringArray searchPaths) override
    {
       StringArray args = {heavyExecutable.getFullPathName(), pdPatch, "-o" + outdir};
       
       if(name.isNotEmpty()) {
           args.add("-n" + name);
       }
       else {
           args.add("-nUntitled");
       }
       
       if(copyright.isNotEmpty()) {
           args.add("--copyright");
           args.add("\"" + name + "\"");
       }
        
        auto boards = StringArray{"seed", "pod", "petal", "patch"};
        auto board = boards[targetBoard.getSelectedId() - 1];
       
       args.add("-m" + createMetadata("daisy", {{"board", board}}));
       
       args.add("-gdaisy");
       
       start(args);
       startThread(3);
       startTimer(15);
   }
    
    void finishExport(String outPath) override
    {
        auto bin = toolchain.getChildFile("bin");
        auto libDaisy = toolchain.getChildFile("lib").getChildFile("libDaisy");
        
#if JUCE_WINDOWS
        auto make = bin.getChildFile("make.exe");
        auto compiler = bin.getChildFile("arm-none-eabi-gcc.exe");
#else
        auto make = bin.getChildFile("make");
        auto compiler = bin.getChildFile("arm-none-eabi-gcc");
#endif

                
        // TODO: thread safety

        auto outputFile = File(outPath);
        libDaisy.copyDirectoryTo(outputFile.getChildFile("libDaisy"));
        
        outputFile.getChildFile("ir").deleteRecursively();
        outputFile.getChildFile("hv").deleteRecursively();
        outputFile.getChildFile("c").deleteRecursively();
        
        auto workingDir = File::getCurrentWorkingDirectory();
        auto sourceDir = outputFile.getChildFile("daisy").getChildFile("source");
        
        sourceDir.setAsCurrentWorkingDirectory();
        
        sourceDir.getChildFile("build").createDirectory();
        toolchain.getChildFile("lib").getChildFile("heavy-static.a").copyFileTo(sourceDir.getChildFile("build").getChildFile("heavy-static.a"));
        toolchain.getChildFile("share").getChildFile("daisy_makefile").copyFileTo(sourceDir.getChildFile("Makefile"));
        
        auto gccPath = toolchain.getChildFile("bin").getFullPathName();
        
        auto projectName = projectNameEditor.getText();
        
#if JUCE_WINDOWS
        String command = "cd " + sourceDir.getFullPathName() + " && " + make.getFullPathName() + " -j4 -f " + sourceDir.getChildFile("Makefile").getFullPathName() + " GCC_PATH=" + gccPath + " PROJECT_NAME=" + projectName;
#else
        String command = "cd " + sourceDir.getFullPathName() + " && " + make.getFullPathName() + " -j4 -f " + sourceDir.getChildFile("Makefile").getFullPathName() + " GCC_PATH=" + gccPath + " PROJECT_NAME=" + projectName;
#endif
        // Use std::system because on Mac, juce ChildProcess is slow when using Rosetta
        std::system(command.toRawUTF8());

        workingDir.setAsCurrentWorkingDirectory();
        
    }
    
    void resized() override
    {
        ExporterSettingsPanel::resized();
        
        auto b = Rectangle<int>(proportionOfWidth (0.1f), 172, proportionOfWidth (0.8f), getHeight() - 172);
        targetBoard.setBounds(b.removeFromTop(23).withTrimmedLeft(labelWidth));
    }
    
    void paint(Graphics& g) override
    {
        ExporterSettingsPanel::paint(g);
        
        auto b = Rectangle<int>(proportionOfWidth (0.1f), 172, proportionOfWidth (0.8f), getHeight() - 172);
        
        g.setFont(Font(15));
        g.setColour(findColour(PlugDataColour::panelTextColourId));
        g.drawText("Target board", b.removeFromTop(23), Justification::centredLeft);
    }
    
};

class ExporterPanel  : public Component, private ListBoxModel
{
public:
    
    ListBox listBox;
    
    TextButton addButton = TextButton(Icons::Add);
    
    OwnedArray<ExporterSettingsPanel> views;
    
    StringArray enabledItems;
    
    std::function<void(int)> onChange;
    
    StringArray items = {"C++", "Daisy"};

    ExporterPanel(PlugDataPluginEditor* editor)
    {
        
        addChildComponent(views.add(new CppSettingsPanel(editor)));
        addChildComponent(views.add(new DaisySettingsPanel(editor)));
        
        addAndMakeVisible(listBox);
        
        listBox.setModel(this);
        listBox.setOutlineThickness(0);
        listBox.selectRow(0);
        listBox.setColour(ListBox::backgroundColourId, Colours::transparentBlack);
        listBox.setRowHeight(28);
    }
    
    void paint(Graphics& g) override
    {
        auto listboxBounds = getLocalBounds().removeFromLeft(200);
        
        g.setColour(findColour(PlugDataColour::panelBackgroundColourId));
        g.fillRoundedRectangle(listboxBounds.toFloat(), 5.0f);
        g.fillRect(listboxBounds.removeFromRight(10));
    }
    
    void selectedRowsChanged (int lastRowSelected) override
    {
        for(int i = 0; i < views.size(); i++)
        {
            views[i]->setVisible(i == lastRowSelected);
        }
    }
    
    void resized() override
    {
        auto b = getLocalBounds();
        listBox.setBounds(b.removeFromLeft(200));
        
        for(auto* view : views)
        {
            view->setBounds(b);
        }
    }
    
    int getNumRows() override
    {
        return items.size();
    }
    
    StringArray getExports() {
        return items;
    }
    
    void paintListBoxItem (int row, Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (isPositiveAndBelow (row, items.size()))
        {
            if (rowIsSelected) {
                g.setColour(findColour (PlugDataColour::panelActiveBackgroundColourId));
                g.fillRoundedRectangle(5, 3, width - 10, height - 6, 5.0f);
            }
            
            auto item = items[row];
            
            auto textBounds = Rectangle<int>(15, 0, width - 30, height);
            const auto textColour = findColour(rowIsSelected ? PlugDataColour::panelActiveTextColourId : PlugDataColour::panelTextColourId);
            
            AttributedString attributedString { item };
            attributedString.setColour (textColour);
            attributedString.setFont (15);
            attributedString.setJustification (Justification::centredLeft);
            attributedString.setWordWrap (AttributedString::WordWrap::none);
            
            TextLayout textLayout;
            textLayout.createLayout (attributedString,
                                     (float) textBounds.getWidth(),
                                     (float) textBounds.getHeight());
            textLayout.draw (g, textBounds.toFloat());
        }
    }
    
    int getBestHeight (int preferredHeight)
    {
        auto extra = listBox.getOutlineThickness() * 2;
        
        return jmax (listBox.getRowHeight() * 2 + extra,
                     jmin (listBox.getRowHeight() * getNumRows() + extra,
                           preferredHeight));
    }

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExporterPanel)
};

struct ToolchainInstaller : public Component, public Thread
{
    struct InstallButton : public Component
    {
        
#if JUCE_WINDOWS
        String downloadSize = "4 GB";
#else
        String downloadSize = "1 GB";
#endif
        
        String iconText = Icons::SaveAs;
        String topText = "Download Toolchain";
        String bottomText = "Download compilation utilities (" + downloadSize + ")";
        
        std::function<void(void)> onClick = [](){};
        
        InstallButton()
        {
            setInterceptsMouseClicks(true, false);
        }
        
        
        void paint(Graphics& g)
        {
            auto* lnf = dynamic_cast<PlugDataLook*>(&getLookAndFeel());
            
            auto textColour = findColour(PlugDataColour::canvasTextColourId);
            if(!isEnabled()) {
                textColour = textColour.brighter(0.4f);
            }
            
            g.setColour(textColour);
            
            g.setFont(lnf->iconFont.withHeight(24));
            g.drawText(iconText, 20, 5, 40, 40, Justification::centredLeft);
            
            g.setFont(lnf->defaultFont.withHeight(16));
            g.drawText(topText, 60, 7, getWidth() - 60, 20, Justification::centredLeft);
            
            g.setFont(lnf->thinFont.withHeight(14));
            g.drawText(bottomText, 60, 25, getWidth() - 60, 16, Justification::centredLeft);
            
            if(isMouseOver() && isEnabled()) {
                g.drawRoundedRectangle(1, 1, getWidth() - 2, getHeight() - 2, 4.0f, 0.5f);
            }
        }
        
        void mouseUp(const MouseEvent& e)
        {
            if(isEnabled()) {
                onClick();
            }
        }
        
        void mouseEnter(const MouseEvent& e)
        {
            repaint();
        }
        
        void mouseExit(const MouseEvent& e)
        {
            repaint();
        }
    };
    
    
#if JUCE_LINUX
    std::tuple<String, String, String> getDistroID()
    {
        ChildProcess catProcess;
        catProcess.start({"cat", "/etc/os-release"});
        
        auto ret = catProcess.readAllProcessOutput();
        
        auto items = StringArray::fromLines(String(ret));
        
        String name;
        String idLike;
        String version;
        
        for(auto& item : items) {
            if(item.startsWith("ID=")) {
                name = item.fromFirstOccurrenceOf("=", false, false).trim();
            }
            else if(item.startsWith("ID_LIKE=")) {
                idLike = item.fromFirstOccurrenceOf("=", false, false).trim();
            }
            else if(item.startsWith("VERSION_ID=")) {
                version = item.fromFirstOccurrenceOf("=", false, false).trim();
            }
        }
        
        return {name, idLike, version};
    }
#endif
    
    ToolchainInstaller() : Thread("Toolchain Install Thread") {
        addAndMakeVisible(&installButton);
        
#if (JUCE_LINUX || JUCE_WINDOWS) && (!defined(__x86_64__) && !defined(_M_X64))
        installButton.setEnabled(false);
#endif
        
        installButton.onClick = [this](){
            
            String downloadLocation = "https://github.com/timothyschoen/HeavyDistributable/releases/download/v0.0.2/";
                        
#if JUCE_MAC
            downloadLocation += "Heavy-MacOS-Universal.zip";
#elif JUCE_WINDOWS
            downloadLocation += "Heavy-Win64.zip";
#else
            
            auto [distroName, distroBackupName, distroVersion] = getDistroID();
            
            if(distroName == "fedora" && distroVersion == "36") {
                downloadLocation += "Heavy-Fedora-36-x64.zip";
            }
            else if(distroName == "fedora" && distroVersion == "35") {
                downloadLocation += "Heavy-Fedora-35-x64.zip";
            }
            else if(distroName == "ubuntu" && distroVersion == "22.04") {
                downloadLocation += "Heavy-Ubuntu-22.04-x64.zip";
            }
            else if(distroBackupName == "ubuntu" || (distroName == "ubuntu" && distroVersion == "20.04")) {
                downloadLocation += "Heavy-Ubuntu-20.04-x64.zip";
            }
            else if(distroName == "arch" || distroBackupName == "arch") {
                downloadLocation += "Heavy-Arch-x64.zip";
            }
            else if(distroName == "debian" || distroBackupName == "debian") {
                downloadLocation += "Heavy-Debian-x64.zip";
            }
            else if(distroName == "opensuse-leap" || distroBackupName.contains("suse")) {
                downloadLocation += "Heavy-OpenSUSE-Leap-x64.zip";
            }
            else if(distroName == "mageia") {
                downloadLocation += "Heavy-Mageia-x64.zip";
            }
            // If we're not sure, just try the debian one and pray
            else {
                downloadLocation += "Heavy-Debian-x64.zip";
            }
#endif
            
            instream = URL(downloadLocation).createInputStream(URL::InputStreamOptions(URL::ParameterHandling::inAddress)
                                                               .withConnectionTimeoutMs(5000)
                                                               .withStatusCode(&statusCode));
            
            startThread(3);
        };
    }
        
    void paint(Graphics& g) override {
        
        auto* lnf = dynamic_cast<PlugDataLook*>(&getLookAndFeel());
        if(!lnf) return;
        
#if (JUCE_LINUX || JUCE_WINDOWS) && (!defined(__x86_64__) && !defined(_M_X64))
        
        g.setColour(findColour(PlugDataColour::canvasTextColourId));
        g.setFont(lnf->boldFont.withHeight(32));
        g.drawText("Non x64 platform not supported", 0, getHeight() / 2 - 150, getWidth(), 40, Justification::centred);
        
        g.setFont(lnf->thinFont.withHeight(23));
        g.drawText("We're working on it!", 0,  getHeight() / 2 - 120, getWidth(), 40, Justification::centred);
#else
        
        g.setColour(findColour(PlugDataColour::canvasTextColourId));
        g.setFont(lnf->boldFont.withHeight(32));
        g.drawText("Toolchain not found", 0, getHeight() / 2 - 150, getWidth(), 40, Justification::centred);
        
        g.setFont(lnf->thinFont.withHeight(23));
        g.drawText("Install the toolchain to get started", 0,  getHeight() / 2 - 120, getWidth(), 40, Justification::centred);
#endif
        
        if(installProgress != 0.0f)
        {
            float width = getWidth() - 90.0f;
            float right = jmap(installProgress, 90.f, width);
            
            Path downloadPath;
            downloadPath.addLineSegment({ 90, 300, right, 300 }, 1.0f);
            
            Path fullPath;
            fullPath.addLineSegment({ 90, 300, width, 300 }, 1.0f);
            
            g.setColour(findColour(PlugDataColour::panelTextColourId));
            g.strokePath(fullPath, PathStrokeType(11.0f, PathStrokeType::JointStyle::curved, PathStrokeType::EndCapStyle::rounded));
            
            g.setColour(findColour(PlugDataColour::panelActiveBackgroundColourId));
            g.strokePath(downloadPath, PathStrokeType(8.0f, PathStrokeType::JointStyle::curved, PathStrokeType::EndCapStyle::rounded));
        }
    }
    
    void resized() override
    {
        installButton.setBounds(getLocalBounds().withSizeKeepingCentre(450, 50).translated(15, -30));
    }
    
    void run() override
    {
        MemoryBlock dekData;
        
        if(!instream) return; // error!
        
        int64 totalBytes = instream->getTotalLength();
        int64 bytesDownloaded = 0;
        
        MemoryOutputStream mo(dekData, true);
        
        while (true) {
            if (threadShouldExit()) {
                //finish(Result::fail("Download cancelled"));
                return;
            }
            
            auto written = mo.writeFromInputStream(*instream, 8192);
            
            if (written == 0)
                break;
            
            bytesDownloaded += written;
            
            float progress = static_cast<long double>(bytesDownloaded) / static_cast<long double>(totalBytes);
            
            MessageManager::callAsync([this, progress]() mutable {
                installProgress = progress;
                repaint();
            });
        }
        
        MemoryInputStream input(dekData, false);
        ZipFile zip(input);
        
        auto result = zip.uncompressTo(toolchain);
        
        if (!result.wasOk()) {
            return;
        }
        
        
#if JUCE_MAC || JUCE_LINUX
        system(("chmod +x " + toolchain.getFullPathName() + "/bin/Heavy/Heavy").toRawUTF8());
#endif
        
        installProgress = 0.0f;
        
        MessageManager::callAsync([this](){
            toolchainInstalledCallback();
        });
    }
    
    inline static File toolchain = File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getChildFile("PlugData").getChildFile("Toolchain");
    
    float installProgress = 0.0f;
    
    
    int statusCode;
    
    InstallButton installButton;
    std::function<void()> toolchainInstalledCallback;
    
    std::unique_ptr<InputStream> instream;
};




struct HeavyExportDialog : public Component
{
    
    bool hasToolchain = false;
    
    ToolchainInstaller installer;
    ExporterPanel exporterPanel;
    
    inline static File toolchain = File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getChildFile("PlugData").getChildFile("Toolchain");
    
    HeavyExportDialog(Dialog* dialog) : exporterPanel(dynamic_cast<PlugDataPluginEditor*>(dialog->parentComponent)) {
        hasToolchain = toolchain.exists();
        
        addChildComponent(installer);
        addChildComponent(exporterPanel);
        
        installer.toolchainInstalledCallback = [this](){
            hasToolchain = true;
            exporterPanel.setVisible(true);
            installer.setVisible(false);
        };
        
        if(hasToolchain) {
            exporterPanel.setVisible(true);
        }
        else {
            installer.setVisible(true);
        }
    }
    
    
    void paint(Graphics& g)
    {
        g.setColour(findColour(PlugDataColour::canvasBackgroundColourId));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
    }
    
    void resized() {
        
        auto b = getLocalBounds();
        exporterPanel.setBounds(b);
        installer.setBounds(b);
        
    }
    
};
