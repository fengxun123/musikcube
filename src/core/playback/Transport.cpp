//////////////////////////////////////////////////////////////////////////////
// Copyright � 2007, Daniel �nnerby
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without 
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright 
//      notice, this list of conditions and the following disclaimer in the 
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may 
//      be used to endorse or promote products derived from this software 
//      without specific prior written permission. 
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
// POSSIBILITY OF SUCH DAMAGE. 
//
//////////////////////////////////////////////////////////////////////////////

#include "pch.hpp"

#include <core/debug.h>
#include <core/playback/Transport.h>
#include <core/plugin/PluginFactory.h>

#include <boost/thread/future.hpp>

using namespace musik::core::audio;

static std::string TAG = "Transport";

#define RESET_NEXT_PLAYER() \
    delete this->nextPlayer; \
    this->nextPlayer = NULL;

static void pausePlayer(Player* p) {
    p->Pause();
}

static void resumePlayer(Player* p) {
    p->Resume();
}

static void deletePlayer(Player* p) {
    delete p;
}

Transport::Transport()
: volume(1.0) 
, state(PlaybackStopped)
, nextPlayer(NULL) {
    this->output = Player::CreateDefaultOutput();
}

Transport::~Transport() {
}

Transport::PlaybackState Transport::GetPlaybackState() {
    boost::recursive_mutex::scoped_lock lock(this->stateMutex);
    return this->state;
}

void Transport::PrepareNextTrack(const std::string& trackUrl) {
    boost::recursive_mutex::scoped_lock lock(this->stateMutex);
    this->nextPlayer = new Player(trackUrl, this->volume, this->output);
}

void Transport::Start(const std::string& url) {
    musik::debug::info(TAG, "we were asked to start the track at " + url);

    Player* newPlayer = new Player(url, this->volume, this->output);
    musik::debug::info(TAG, "Player created successfully");

    this->StartWithPlayer(newPlayer);
}

void Transport::StartWithPlayer(Player* newPlayer) {
    if (newPlayer) {
        {
            boost::recursive_mutex::scoped_lock lock(this->stateMutex);

            if (newPlayer != nextPlayer) {
                delete nextPlayer;
            }

            this->nextPlayer = NULL;

            newPlayer->PlaybackStarted.connect(this, &Transport::OnPlaybackStarted);
            newPlayer->PlaybackAlmostEnded.connect(this, &Transport::OnPlaybackAlmostEnded);
            newPlayer->PlaybackFinished.connect(this, &Transport::OnPlaybackFinished);
            newPlayer->PlaybackStopped.connect(this, &Transport::OnPlaybackStopped);
            newPlayer->PlaybackError.connect(this, &Transport::OnPlaybackError);

            musik::debug::info(TAG, "play()");

            this->active.push_front(newPlayer);
            newPlayer->SetVolume(this->volume);
            newPlayer->Play();
        }

        this->RaiseStreamEvent(Transport::StreamScheduled, newPlayer);
    }
}

void Transport::Stop() {
    musik::debug::info(TAG, "stop");

    std::list<Player*> toDelete;

    {
        boost::recursive_mutex::scoped_lock lock(this->stateMutex);

        RESET_NEXT_PLAYER();
        std::swap(toDelete, this->active);
    }

    /* do the actual delete outside of the critical section! the players run 
    in a background thread that will emit a signal on completion, but the 
    destructor joins(). */
    std::for_each(toDelete.begin(), toDelete.end(), deletePlayer);
    this->active.clear();

    this->SetPlaybackState(PlaybackStopped);
}

bool Transport::Pause() {
    musik::debug::info(TAG, "pause");

    size_t count = 0;

    {
        boost::recursive_mutex::scoped_lock lock(this->stateMutex);
        std::for_each(this->active.begin(), this->active.end(), pausePlayer);
        count = this->active.size();
    }

    if (count) {
        this->SetPlaybackState(PlaybackPaused);
        return true;
    }

    return false;
}

bool Transport::Resume() {
    musik::debug::info(TAG, "resume");

    size_t count = 0;
    
    {
        boost::recursive_mutex::scoped_lock lock(this->stateMutex);
        std::for_each(this->active.begin(), this->active.end(), resumePlayer);
        count = this->active.size();
    }

    if (count) {
        this->SetPlaybackState(Transport::PlaybackPlaying);
        return true;
    }

    return false;
}

double Transport::Position() {
    boost::recursive_mutex::scoped_lock lock(this->stateMutex);

    if (!this->active.empty()) {
        return this->active.front()->Position();
    }

    return 0;
}

void Transport::SetPosition(double seconds) {
    boost::recursive_mutex::scoped_lock lock(this->stateMutex);
    
    if (!this->active.empty()) {
        this->active.front()->SetPosition(seconds);
        this->TimeChanged(seconds);
    }
}

double Transport::Volume() {
    return this->volume;
}

void Transport::SetVolume(double volume) {
    double oldVolume = this->volume;
    
    volume = max(0, min(1.0, volume));

    this->volume = volume;

    if (oldVolume != this->volume) {
        this->VolumeChanged();
    }

    musik::debug::info(TAG, boost::str(
        boost::format("set volume %d%%") % round(volume * 100)));

    {
        boost::recursive_mutex::scoped_lock lock(this->stateMutex);

        if (!this->active.empty()) {
            this->active.front()->SetVolume(volume);
        }
    }
}

void Transport::OnPlaybackStarted(Player* player) {
    this->RaiseStreamEvent(Transport::StreamPlaying, player);
    this->SetPlaybackState(Transport::PlaybackPlaying);
}

void Transport::OnPlaybackAlmostEnded(Player* player) {
    this->RaiseStreamEvent(Transport::StreamAlmostDone, player);
}

void Transport::RemoveActive(Player* player) {
    boost::recursive_mutex::scoped_lock lock(this->stateMutex);

    std::list<Player*>::iterator it =
        std::find(this->active.begin(), this->active.end(), player);

    if (it != this->active.end()) {
        delete (*it);
        this->active.erase(it);
    }
}

void Transport::OnPlaybackFinished(Player* player) {
    this->RaiseStreamEvent(Transport::StreamFinished, player);

    if (this->nextPlayer) {
        this->StartWithPlayer(this->nextPlayer);
    }
    else {
        this->SetPlaybackState(Transport::PlaybackStopped);
    }

    boost::async(boost::bind(&Transport::RemoveActive, this, player));
}

void Transport::OnPlaybackStopped (Player* player) {
    this->RaiseStreamEvent(Transport::StreamStopped, player);
    this->SetPlaybackState(Transport::PlaybackStopped);
    boost::async(boost::bind(&Transport::RemoveActive, this, player));
}

void Transport::OnPlaybackError(Player* player) {
    this->RaiseStreamEvent(Transport::StreamError, player);
    this->SetPlaybackState(Transport::PlaybackStopped);
    boost::async(boost::bind(&Transport::RemoveActive, this, player));
}

void Transport::SetPlaybackState(int state) {
    bool changed = false;

    {
        boost::recursive_mutex::scoped_lock lock(this->stateMutex);
        this->state = (PlaybackState) state;
    }

    if (changed) {
        this->PlaybackEvent(state);
    }
}

void Transport::RaiseStreamEvent(int type, Player* player) {
    this->StreamEvent(type, player->GetUrl());
}