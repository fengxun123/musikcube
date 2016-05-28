#include "stdafx.h"

#include <cursespp/Screen.h>
#include <cursespp/Colors.h>
#include <cursespp/MessageQueue.h>
#include <cursespp/Message.h>

#include "CommandWindow.h"

#include <core/debug.h>
#include <core/sdk/IPlugin.h>
#include <core/plugin/PluginFactory.h>
#include <core/library/Indexer.h>
#include <core/library/track/Track.h>
#include <core/library/Indexer.h>

#include <boost/algorithm/string.hpp>

#define MAX_SIZE 2046

using musik::core::Indexer;
using musik::core::IndexerPtr;
using musik::core::LibraryFactory;
using musik::core::LibraryPtr;
using musik::core::TrackPtr;
using musik::core::QueryPtr;

using namespace musik::core::audio;
using namespace musik::box;
using namespace cursespp;

template <class T>
bool tostr(T& t, const std::string& s) {
    std::istringstream iss(s);
    return !(iss >> t).fail();
}

CommandWindow::CommandWindow(
    IWindow *parent,
    Transport& transport,
    LibraryPtr library,
    OutputWindow& output)
: Window(parent) {
    this->SetContentColor(BOX_COLOR_WHITE_ON_BLACK);
    this->transport = &transport;
    this->library = library;
    this->bufferPosition = 0;
    this->output = &output;
    this->paused = false;
    this->output->WriteLine("type 'h' or 'help'\n", BOX_COLOR_BLACK_ON_GREY);
}

CommandWindow::~CommandWindow() {
}

void CommandWindow::Show() {
    Window::Show();
    wmove(this->GetContent(), 0, 0);
    wprintw(this->GetContent(), "%s", buffer.c_str());
}

void CommandWindow::Focus() {
    wmove(this->GetContent(), 0, bufferPosition);
}

void removeUtf8Char(std::string& value) {
    std::string::iterator it = value.end();
    std::string::iterator start = value.begin();
    if (it != start) {
        utf8::prior(it, start);
        value = std::string(value.begin(), it);
    }
}

void CommandWindow::Write(const std::string& key) {
    if (key == "^H" || key == "^?") { /* backspace */
        removeUtf8Char(this->buffer);
    }
    else if (key == "^J") { /* return */
        output->WriteLine("> " + this->buffer + "\n", COLOR_PAIR(BOX_COLOR_BLACK_ON_GREY));

        if (!this->ProcessCommand(this->buffer)) {
            if (this->buffer.size()) {
                output->WriteLine(
                    "illegal command: '" +
                    this->buffer +
                    "'\n", COLOR_PAIR(BOX_COLOR_RED_ON_GREY));
            }
        }

        this->buffer = "";
    }
    else {
        /* one character at a time. if it's more than one character, we're
        dealing with an escape sequence and should not print it. */
        if (u8len(key) == 1) {
            this->buffer += key;
        }
    }

    this->Clear();
    wprintw(this->GetContent(), buffer.c_str());

    this->Repaint();
}

void CommandWindow::Seek(const std::vector<std::string>& args) {
    if (args.size() > 0) {
        double newPosition = 0;
        if (tostr<double>(newPosition, args[0])) {
            transport->SetPosition(newPosition);
        }
    }
}

void CommandWindow::SetVolume(const std::vector<std::string>& args) {
    if (args.size() > 0) {
        float newVolume = 0;
        if (tostr<float>(newVolume, args[0])) {
            this->SetVolume(newVolume / 100.0f);
        }
    }
}

void CommandWindow::SetVolume(float volume) {
    transport->SetVolume(volume);
}

void CommandWindow::Help() {
    int64 s = -1;
    this->output->WriteLine("help:\n", s);
    this->output->WriteLine("  <tab> to switch between windows\n", s);
    this->output->WriteLine("  pl [file]: play file at path", s);
    this->output->WriteLine("  pa: toggle pause/resume", s);
    this->output->WriteLine("  st: stop playing", s);
    this->output->WriteLine("  plugins: list loaded plugins", s);
    this->output->WriteLine("  v: <0 - 100>: set % volume", s);
    this->output->WriteLine("  sk <seconds>: seek to <seconds> into track", s);
    this->output->WriteLine("  addir <dir>: add a directory to be indexed", s);
    this->output->WriteLine("  rmdir <dir>: remove indexed directory path", s);
    this->output->WriteLine("  lsdirs: list all directories used by the indexer", s);
    this->output->WriteLine("  rescan: rescan metadata in index paths", s);
    this->output->WriteLine("\n  <ctrl+d>: quit\n", s);
}

bool CommandWindow::ProcessCommand(const std::string& cmd) {
    std::vector<std::string> args;
    boost::algorithm::split(args, cmd, boost::is_any_of(" "));

    std::string name = args.size() > 0 ? args[0] : "";
    args.erase(args.begin());

    if (name == "plugins") {
        this->ListPlugins();
    }
    else if (name == "play" || name == "pl" || name == "p") {
        return this->PlayFile(args);
    }
    else if (name == "addir") {
        std::string path = boost::algorithm::join(args, " ");
        library->Indexer()->AddPath(path);
    }
    else if (name == "rmdir") {
        std::string path = boost::algorithm::join(args, " ");
        library->Indexer()->RemovePath(path);
    }
    else if (name == "lsdirs") {
        std::vector<std::string> paths;
        library->Indexer()->GetPaths(paths);
        this->output->WriteLine("paths:");
        for (size_t i = 0; i < paths.size(); i++) {
            this->output->WriteLine("  " + paths.at(i));
        }
        this->output->WriteLine("");
    }
    else if (name == "rescan" || name == "scan" || name == "index") {
        library->Indexer()->Synchronize();
    }
    else if (name == "h" || name == "help") {
        this->Help();
    }
    else if (cmd == "pa" || cmd == "pause") {
        this->Pause();
    }
    else if (cmd == "s" || cmd =="stop") {
        this->Stop();
    }
    else if (name == "sk" || name == "seek") {
        this->Seek(args);
    }
    else if (name == "plugins") {
        this->ListPlugins();
    }
    else if (name == "v" || name == "volume") {
        this->SetVolume(args);
    }
    else {
        return false;
    }


    return true;
}

bool CommandWindow::PlayFile(const std::vector<std::string>& args) {
    if (args.size() > 0) {
        std::string filename = boost::algorithm::join(args, " ");
        transport->Start(filename);
        return true;
    }

    return false;
}

void CommandWindow::Pause() {
    if (this->paused) {
        transport->Resume();
        this->paused = !this->paused;
    }
    else {
        transport->Pause();
        this->paused = !this->paused;
    }
}

void CommandWindow::Stop() {
    this->transport->Stop();
}

void CommandWindow::ListPlugins() const {
    using musik::core::IPlugin;
    using musik::core::PluginFactory;

    typedef std::vector<std::shared_ptr<IPlugin> > PluginList;
    typedef PluginFactory::NullDeleter<IPlugin> Deleter;

    PluginList plugins = PluginFactory::Instance()
        .QueryInterface<IPlugin, Deleter>("GetPlugin");

    PluginList::iterator it = plugins.begin();
    for (; it != plugins.end(); it++) {
        std::string format =
            "plugin:\n"
            "  name: " + std::string((*it)->Name()) + " "
            "v" + std::string((*it)->Version()) + "\n"
            "  author: " + std::string((*it)->Author()) + "\n";

        this->output->WriteLine(format, BOX_COLOR_RED_ON_BLUE);
    }
}
