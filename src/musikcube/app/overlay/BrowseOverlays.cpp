//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007-2017 musikcube team
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

#include "stdafx.h"

#include "BrowseOverlays.h"
#include <core/library/query/local/AllCategoriesQuery.h>
#include <cursespp/SimpleScrollAdapter.h>
#include <cursespp/ListOverlay.h>
#include <cursespp/DialogOverlay.h>
#include <cursespp/App.h>
#include <set>

using namespace cursespp;
using namespace musik::cube;
using namespace musik::core;
using namespace musik::core::db;
using namespace musik::core::db::local;

static std::set<std::string> BLACKLIST = { "bitrate", "channels", "lyrics", "path_id" };
static std::string LAST_SELECTED;

static void showNoPathsError() {
    std::shared_ptr<DialogOverlay> dialog(new DialogOverlay());

    (*dialog)
        .SetTitle(_TSTR("browse_no_paths_overlay_error_title"))
        .SetMessage(_TSTR("browse_no_paths_overlay_error_message"))
        .AddButton("KEY_ENTER", "ENTER", _TSTR("button_ok"));

    App::Overlays().Push(dialog);
}

void BrowseOverlays::ShowCategoryChooser(
    musik::core::ILibraryPtr library,
    std::function<void(std::string)> callback)
{
    using Adapter = cursespp::SimpleScrollAdapter;
    using ListOverlay = cursespp::ListOverlay;
    using Value = SdkValue::Shared;

    auto query = std::make_shared<AllCategoriesQuery>();
    library->Enqueue(query, ILibrary::QuerySynchronous);

    std::shared_ptr<Adapter> adapter(new Adapter());
    adapter->SetSelectable(true);

    size_t index = 0;
    auto filtered = query->GetResult()->Filter([&index, adapter](const Value& value) -> bool {
        auto str = value->ToString();
        if (BLACKLIST.find(str) == BLACKLIST.end()) {
            adapter->AddEntry(str);
            if (LAST_SELECTED == str) {
                index = adapter->GetEntryCount() - 1;
            }
            return true;
        }
        return false;
    });

    std::shared_ptr<ListOverlay> dialog(new ListOverlay());

    dialog->SetAdapter(adapter)
        .SetTitle(_TSTR("browse_categories_title"))
        .SetWidth(_DIMEN("browse_categories_overlay_width", 35))
        .SetSelectedIndex(index)
        .SetItemSelectedCallback(
        [callback, filtered]
        (ListOverlay* overlay, IScrollAdapterPtr adapter, size_t index) {
            LAST_SELECTED = filtered->At(index)->ToString();
            callback(LAST_SELECTED);
        });

    cursespp::App::Overlays().Push(dialog);
}

void BrowseOverlays::ShowDirectoryChooser(
    musik::core::ILibraryPtr library,
    std::function<void(std::string)> callback)
{
    using StringList = std::vector<std::string>;
    using Adapter = cursespp::SimpleScrollAdapter;

    std::shared_ptr<StringList> paths = std::make_shared<StringList>();
    library->Indexer()->GetPaths(*paths.get());

    if (paths->size() == 0) {
        showNoPathsError();
    }
    else {
        std::shared_ptr<Adapter> adapter(new Adapter());
        adapter->SetSelectable(true);
        for (auto path : *paths) {
            adapter->AddEntry(path);
        }

        std::shared_ptr<ListOverlay> dialog(new ListOverlay());

        dialog->SetAdapter(adapter)
            .SetTitle(_TSTR("browse_pick_path_overlay_title"))
            .SetWidthPercent(80)
            .SetSelectedIndex(0)
            .SetItemSelectedCallback(
                [paths, callback]
                (ListOverlay* overlay, IScrollAdapterPtr adapter, size_t index) {
                    if (callback) {
                        callback(paths->at(index));
                    }
                });

        cursespp::App::Overlays().Push(dialog);
    }
}

void BrowseOverlays::ShowIndexer(musik::core::ILibraryPtr library) {
    using Adapter = cursespp::SimpleScrollAdapter;
    std::shared_ptr<Adapter> adapter(new Adapter());
    adapter->AddEntry(_TSTR("indexer_overlay_reindex"));
    adapter->AddEntry(_TSTR("indexer_overlay_rebuild"));
    adapter->SetSelectable(true);

    std::shared_ptr<ListOverlay> dialog(new ListOverlay());

    dialog->SetAdapter(adapter)
        .SetTitle(_TSTR("indexer_overlay_title"))
        .SetWidth(_DIMEN("indexer_overlay_width", 28))
        .SetSelectedIndex(0)
        .SetItemSelectedCallback(
        [library]
        (ListOverlay* overlay, IScrollAdapterPtr adapter, size_t index) {
            switch (index) {
                case 0: library->Indexer()->Schedule(IIndexer::SyncType::Local); break;
                case 1: library->Indexer()->Schedule(IIndexer::SyncType::Rebuild); break;
            }
        });

    cursespp::App::Overlays().Push(dialog);
}