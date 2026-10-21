/**
 * @file app/streaming/seamless/seamlesspresenter.mm
 * @brief One local window per remote window, on unmodified renderer instances.
 * @details The renderer file is not touched: each window owns an SDL window and an
 *          instance of the same renderer class the session uses, initialized
 *          through the pristine PDECODER_PARAMETERS contract, and receives the same
 *          decoded frames the session's renderer receives. The crop is a layer
 *          translation clamped to the window bounds, so the instance draws the
 *          whole canvas and the window shows its cell. Every AppKit/SDL call is on
 *          the main thread and guarded, so a failure degrades to "no window"
 *          rather than to a torn-down process - the same discipline the window
 *          creation in seamlesswindow.mm already follows.
 */
#include "seamlesspresenter.h"

#include <map>
#include <string>

#include <SDL.h>

#include "seamlessmodel.h"
#include "seamlesswindowsmodel.h"

#include "../video/decoder.h"
#include "../video/ffmpeg-renderers/renderer.h"
#include "../video/ffmpeg-renderers/vt.h"

namespace seamless_presenter {

namespace {

struct Instance {
    SDL_Window* window = nullptr;
    IFFmpegRenderer* renderer = nullptr;
    QPoint cellPos;
    QSize cellSize;
    QSize clientSize;
    quint8 flags = 0;
};

std::map<quint32, Instance> g_instances;

// The format the session decodes to. The presenter does not decide the decoder;
// it mirrors the session, and the renderer instance converts as it always does.
int g_videoFormat = 0;
int g_frameRate = 60;

void destroyInstance(Instance& instance)
{
    if (instance.renderer != nullptr) {
        // The renderer owns its Metal layer and display link; deleting it stops
        // them. Guarded like every other step here.
        @try {
            delete instance.renderer;
        }
        @catch (NSException* exception) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Seamless: releasing a window renderer raised %s: %s",
                         [[exception name] UTF8String], [[exception reason] UTF8String]);
        }
        instance.renderer = nullptr;
    }
    if (instance.window != nullptr) {
        SDL_DestroyWindow(instance.window);
        instance.window = nullptr;
    }
}

bool createInstance(quint32 id, const SeamlessModel::WindowEntry& entry)
{
    Instance instance;
    instance.cellPos = entry.cellPos;
    instance.cellSize = entry.cellSize;
    instance.clientSize = entry.clientSize;
    instance.flags = entry.flags;

    const int w = MAX(entry.clientSize.width(), 1);
    const int h = MAX(entry.clientSize.height(), 1);

    std::string title = "Window";
    @try {
        instance.window = SDL_CreateWindow(title.c_str(),
                                           SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                           w, h,
                                           SDL_WINDOW_METAL | SDL_WINDOW_BORDERLESS);
        if (instance.window == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Seamless: window %u was not created: %s", (unsigned) id, SDL_GetError());
            return false;
        }

        // An instance of the SAME renderer class the session uses, initialized with
        // this window: the pristine contract, nothing new.
        instance.renderer = VTMetalRendererFactory::createRenderer(false);
        if (instance.renderer == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Seamless: window %u has no renderer", (unsigned) id);
            destroyInstance(instance);
            return false;
        }

        DECODER_PARAMETERS params {};
        params.window = instance.window;
        params.width = w;
        params.height = h;
        params.videoFormat = g_videoFormat;
        params.frameRate = g_frameRate;
        params.enableVsync = true;
        params.enableFramePacing = true;
        params.testOnly = false;

        if (!instance.renderer->initialize(&params)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Seamless: window %u renderer refused its window", (unsigned) id);
            destroyInstance(instance);
            return false;
        }
    }
    @catch (NSException* exception) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Seamless: window %u creation raised %s: %s", (unsigned) id,
                     [[exception name] UTF8String], [[exception reason] UTF8String]);
        destroyInstance(instance);
        return false;
    }

    if ((entry.flags & SEAMLESS_WF_VISIBLE) == 0) {
        SDL_HideWindow(instance.window);
    }
    else {
        SDL_ShowWindow(instance.window);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Seamless: window %u presented locally, %dx%d, cell %d,%d %dx%d",
                (unsigned) id, w, h, entry.cellPos.x(), entry.cellPos.y(),
                entry.cellSize.width(), entry.cellSize.height());

    g_instances[id] = instance;
    return true;
}

} // namespace

void setVideoFormat(int videoFormat, int frameRate)
{
    g_videoFormat = videoFormat;
    g_frameRate = frameRate;
}

void setTable(const std::vector<SeamlessModel::WindowEntry>& table)
{
    // Gone windows go first: a window that left the table must not outlive it.
    for (auto it = g_instances.begin(); it != g_instances.end();) {
        bool present = false;
        for (const auto& entry : table) {
            if (entry.id == it->first) {
                present = true;
                break;
            }
        }
        if (present) {
            ++it;
        }
        else {
            destroyInstance(it->second);
            it = g_instances.erase(it);
        }
    }

    for (const auto& entry : table) {
        auto it = g_instances.find(entry.id);
        if (it == g_instances.end()) {
            createInstance(entry.id, entry);
            continue;
        }

        // A held window follows its cell: the layer translation is refreshed when
        // the cell or the visibility changed.
        Instance& instance = it->second;
        const bool visibleAgain = (entry.flags & SEAMLESS_WF_VISIBLE) != 0;
        if (instance.cellPos != entry.cellPos || instance.cellSize != entry.cellSize ||
            instance.clientSize != entry.clientSize || instance.flags != entry.flags) {
            instance.cellPos = entry.cellPos;
            instance.cellSize = entry.cellSize;
            instance.clientSize = entry.clientSize;
            instance.flags = entry.flags;
            if (visibleAgain) {
                SDL_ShowWindow(instance.window);
            }
            else {
                SDL_HideWindow(instance.window);
            }
        }
    }
}

void syncFromModel()
{
    SeamlessModel* model = GetSeamlessModel();
    if (model == nullptr) {
        return;
    }

    const SeamlessModel::TableView view = model->snapshotTable();
    static size_t lastCount = (size_t) -1;
    static quint32 lastFirstId = 0;
    const quint32 firstId = view.windows.empty() ? 0 : view.windows.front().id;
    if (view.windows.size() == lastCount && firstId == lastFirstId && !g_instances.empty()) {
        return;
    }
    lastCount = view.windows.size();
    lastFirstId = firstId;

    std::vector<SeamlessModel::WindowEntry> entries;
    entries.reserve((size_t) view.windows.size());
    for (const auto& entry : view.windows) {
        entries.push_back(entry);
    }
    setTable(entries);
}

void present(AVFrame* frame)
{
    if (frame == nullptr || g_instances.empty()) {
        return;
    }

    for (auto& pair : g_instances) {
        Instance& instance = pair.second;
        if (instance.renderer == nullptr || instance.window == nullptr) {
            continue;
        }
        if ((instance.flags & SEAMLESS_WF_VISIBLE) == 0) {
            // A hidden remote window stays hidden: its instance is not asked to
            // draw, and it keeps its place in the map for when it returns.
            continue;
        }
        @try {
            instance.renderer->renderFrame(frame);
        }
        @catch (NSException* exception) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Seamless: window %u render raised %s: %s", (unsigned) pair.first,
                         [[exception name] UTF8String], [[exception reason] UTF8String]);
        }
    }
}

void shutdown()
{
    for (auto& pair : g_instances) {
        destroyInstance(pair.second);
    }
    g_instances.clear();
}

bool active()
{
    return !g_instances.empty();
}

} // namespace seamless_presenter
