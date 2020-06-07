//
//  ImguiLogger.cpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 23/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#include "imgui_cvlog.h"

#include "imgui_internal.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <thread>

namespace ImGui
{
namespace CVLog
{

class Window;

class WindowData
{
public:
    static const char* defaultCategoryName() { return "Default"; }
    
public:
    WindowData(const char* windowName)
    {
        _name = windowName;
        _id = ImHashStr(windowName);
    }
    
    const std::string& name () const { return _name; }
    ImGuiID id () const { return _id; }
        
    // Here we hackily rely on a special window settings to avoid having to write
    // our own settings handler and have persistent data.
    bool isVisible() const { return _isVisible; };
    bool& isVisibleRef() { return _isVisible; };

public:
    // Can be nullptr if no window was yet created, but properties were specified.
    Window* window = nullptr;
    
    std::string category = defaultCategoryName();
    
    ImVec2 preferredSize = ImVec2(320,240);
    std::string helpString = "No help specified";
        
    struct
    {
        bool hasData = false;
        ImVec2 pos = ImVec2(0,0);
        ImVec2 size = ImVec2(0,0);
        ImGuiCond_ imGuiCond = ImGuiCond_Always;
    } layoutUpdateOnNextFrame;
        
    std::map<std::string, std::function<void(void)>> preRenderCallbacks;
    
private:
    // Keeping everything to have good performance in Debug too.
    std::string _name;
    ImGuiID _id = 0; // == ImHashStr(name)
    bool _isVisible = true;
};

struct WindowCategory
{
    std::string name;
    std::vector<WindowData*> windows;
};

class WindowManager
{
public:
    static constexpr int windowListWidth = 200;
    
public:
    const std::vector<std::unique_ptr<WindowData>>& windowsData() const { return _windowsData; };
    
    WindowData& AddWindow (const char* windowName, std::unique_ptr<Window> windowPtr)
    {
        Window* window = windowPtr.get();
        _windows.emplace_back(std::move(windowPtr));
        auto& data = FindOrCreateDataForWindow(windowName);
        data.window = window;
        window->imGuiData = &data;
        
        auto& IO = ImGui::GetIO();
        data.layoutUpdateOnNextFrame.size = data.preferredSize;
        
        const float availableWidth = std::max(0.f, (IO.DisplaySize.x - windowListWidth - data.preferredSize.x));
        const float availableHeight = std::max(0.f, (IO.DisplaySize.y - data.preferredSize.y));
        
        data.layoutUpdateOnNextFrame.pos.x = windowListWidth + (float(rand()) / float(RAND_MAX)) * availableWidth;
        data.layoutUpdateOnNextFrame.pos.y = (float(rand()) / float(RAND_MAX)) * availableHeight;
        data.layoutUpdateOnNextFrame.imGuiCond = ImGuiCond_FirstUseEver;
        data.layoutUpdateOnNextFrame.hasData = true;
        
        {
            std::lock_guard<std::mutex> _(concurrent.lock);
            concurrent.windowsByID.SetVoidPtr(data.id(), window);
        }
        
        return data;
    }
    
    // Could take a single set with a Json to know which properties to update.
    WindowData& SetWindowCategory (const char* windowName, const char* newCategory)
    {
        auto& data = FindOrCreateDataForWindow(windowName);
        if (data.category == newCategory)
            return data;
        
        auto& oldCat = findOrCreateCategory(data.category.c_str());
        
        auto oldCatIt = std::find_if(oldCat.windows.begin(), oldCat.windows.end(), [&](WindowData* p) {
            return &data == p;
        });
        oldCat.windows.erase(oldCatIt);
        
        data.category = newCategory;
        auto& newCat = findOrCreateCategory(newCategory);
        newCat.windows.push_back (&data);
        return data;
    }
    
    WindowData& SetWindowPreferredSize (const char* windowName, const ImVec2& preferredSize)
    {
        auto& data = FindOrCreateDataForWindow(windowName);
        data.preferredSize = preferredSize;
        return data;
    }
    
    WindowData& SetWindowHelpString (const char* windowName, const std::string& helpString)
    {
        auto& data = FindOrCreateDataForWindow(windowName);
        data.helpString = helpString;
        return data;
    }
    
    WindowData& FindOrCreateDataForWindow (const char* windowName)
    {
        ImGuiID windowID = ImHashStr(windowName);
        WindowData* data = findDataForWindow(windowID);
        if (data != nullptr)
            return *data;
        return createDataForWindow(windowName, WindowData::defaultCategoryName());
    }
    
    void TileAndScaleVisibleWindows()
    {
        auto& IO = ImGui::GetIO();
        
        auto isWindowHeightSmaller = [](const Window* w1, const Window* w2) {
            if (w1->imGuiData->preferredSize.y < w2->imGuiData->preferredSize.y)
                return true;
            if (w1->imGuiData->preferredSize.y > w2->imGuiData->preferredSize.y)
                return false;
            if (w1->imGuiData->preferredSize.x < w2->imGuiData->preferredSize.x)
                return true;
            if (w1->imGuiData->preferredSize.x > w2->imGuiData->preferredSize.x)
                return false;
            return w1->imGuiData->name() < w2->imGuiData->name();
        };
        
        std::set<Window*, decltype(isWindowHeightSmaller)> windowsSortedBySize(isWindowHeightSmaller);
        for (const auto& win : _windows)
            windowsSortedBySize.insert (win.get());

        const float startX = windowListWidth;
        const float endX = IO.DisplaySize.x;
        const float startY = 0;
        const float endY = IO.DisplaySize.y;
        
        float scaleFactor = 1.0f;
        bool didFit = false;
        while (!didFit)
        {
            float currentX = startX;
            float currentY = startY;
            float maxHeightInCurrentRow = 0;
            
            // Start optimistic.
            didFit = true;
            
            for (auto& win : windowsSortedBySize)
            {
                auto* winData = win->imGuiData;
                if (!winData->isVisible())
                    continue;
                
                ImVec2 scaledWinSize (winData->preferredSize.x*scaleFactor,
                                      winData->preferredSize.y*scaleFactor);
                
                // Start new row?
                if (currentX > startX && currentX + scaledWinSize.x > endX)
                {
                    currentX = startX;
                    currentY += maxHeightInCurrentRow;
                    maxHeightInCurrentRow = 0;
                }
                
                // No more row? Start again from the top, but with an offset.
                if (currentY + scaledWinSize.y > endY)
                {
                    didFit = false;
                    // Retry with a smaller scale.
                    scaleFactor *= 0.95;
                    break;
                }
                
                winData->layoutUpdateOnNextFrame.size = scaledWinSize;
                winData->layoutUpdateOnNextFrame.pos = ImVec2(currentX, currentY);
                winData->layoutUpdateOnNextFrame.imGuiCond = ImGuiCond_Always;
                winData->layoutUpdateOnNextFrame.hasData = true;
                ImGui::SetWindowFocus(winData->name().c_str());
                
                currentX += scaledWinSize.x;
                maxHeightInCurrentRow = std::max(maxHeightInCurrentRow, scaledWinSize.y);
            }
        }
    }
    
    void Render()
    {
        auto& IO = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(windowListWidth, IO.DisplaySize.y), ImGuiCond_Always);
        if (ImGui::Begin("Window List", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        {
            bool allHidden = true;
            for (const auto& win : _windows)
            {
                if (win->imGuiData->isVisible())
                {
                    allHidden = false;
                    break;
                }
            }
            
            if (allHidden)
            {
                if (ImGui::Button("Show All"))
                {
                    for (const auto& win : _windows)
                        win->imGuiData->isVisibleRef() = true;
                }
            }
            else
            {
                if (ImGui::Button("Hide All"))
                {
                    for (const auto& win : _windows)
                        win->imGuiData->isVisibleRef() = false;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Auto-Tile"))
            {
                TileAndScaleVisibleWindows();
            }
            
            for (auto& cat : _windowsPerCategory)
            {
                // 3-state checkbox for all the windows in the category.
                bool showCat = false;
                {
                    const int checkboxWidth = ImGui::GetFrameHeight() - ImGui::GetStyle().FramePadding.x;
                    
                    showCat = ImGui::CollapsingHeader(cat.name.c_str(), ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen);
                    
                    ImGui::SameLine(GetContentRegionMax().x - checkboxWidth);
                    
                    int numVisible = 0;
                    for (auto& winData : cat.windows)
                        numVisible += winData->isVisible();
                    
                    bool mixedState = (numVisible > 0 && numVisible != cat.windows.size());
                    
                    if (mixedState)
                    {
                        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                    }
                    
                    bool selected = (numVisible == cat.windows.size());
                    if (ImGui::Checkbox(("##" + cat.name).c_str(), &selected))
                    {
                        for (auto& winData : cat.windows)
                            winData->isVisibleRef() = selected;
                    }
                    
                    if (mixedState)
                        ImGui::PopItemFlag();
                }
                                
                if (!showCat)
                    continue;
                
                for (auto& winData : cat.windows)
                {
                    const bool disabled = (winData->window == nullptr);
                    if (disabled)
                    {
                        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                    }
                    
                    if (ImGui::Checkbox(winData->name().c_str(), &winData->isVisibleRef()))
                    {
                        // Make sure we save the new visible state.
                        ImGui::MarkIniSettingsDirty();
                    }
                    
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                        ImGui::TextUnformatted(winData->name().c_str());
                        ImGui::TextUnformatted(winData->helpString.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                    
                    if (disabled)
                    {
                        ImGui::PopItemFlag();
                        ImGui::PopStyleVar();
                    }
                }
            }
        }
        ImGui::End();
        
        for (auto& winData : _windowsData)
        {
            if (winData->window && winData->isVisible())
            {
                if (winData->layoutUpdateOnNextFrame.hasData)
                {
                    ImGui::SetNextWindowPos(winData->layoutUpdateOnNextFrame.pos, winData->layoutUpdateOnNextFrame.imGuiCond);
                    ImGui::SetNextWindowSize(winData->layoutUpdateOnNextFrame.size, winData->layoutUpdateOnNextFrame.imGuiCond);
                    ImGui::SetNextWindowCollapsed(false, winData->layoutUpdateOnNextFrame.imGuiCond);
                    winData->layoutUpdateOnNextFrame = {}; // reset it.
                    ImGui::MarkIniSettingsDirty();
                }
                    
                ImGui::Begin(winData->name().c_str(), &winData->isVisibleRef());
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                    ImGui::TextUnformatted(winData->helpString.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::End();
                
                if (!winData->preRenderCallbacks.empty())
                {
                    if (ImGui::Begin(winData->name().c_str()))
                    {
                        for (const auto& it : winData->preRenderCallbacks)
                            it.second();
                    }
                    ImGui::End();
                }
                
                winData->window->Render();
            }
        }
    }
    
    Window* ConcurrentFindWindowById(ImGuiID id)
    {
        void* window = concurrent.windowsByID.GetVoidPtr(id);
        return reinterpret_cast<Window*>(window);
    }

    Window* ConcurrentFindWindow (const char* name)
    {
        ImGuiID id = ImHashStr(name);
        Window* window = ConcurrentFindWindowById(id);
        
        if (window == nullptr)
            return nullptr;
        
        // Non-unique hash? Should be extremely rare! Rename your window if that somehow happens.
        IM_ASSERT (window->imGuiData->name() == name);
        return window;
    }

    
private:
    void helpMarker(const char* desc)
    {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    
    WindowData& createDataForWindow (const char* windowName, const char* categoryName)
    {
        _windowsData.emplace_back(std::make_unique<WindowData>(windowName));
        auto* winData = _windowsData.back().get();
        winData->category = categoryName;
        
        auto& cat = findOrCreateCategory(categoryName);
        cat.windows.emplace_back(winData);
        return *winData;
    }
    
    WindowCategory& findOrCreateCategory(const char* categoryName)
    {
        for (auto& cat : _windowsPerCategory)
            if (cat.name == categoryName)
                return cat;
        _windowsPerCategory.push_back(WindowCategory());
        _windowsPerCategory.back().name = categoryName;
        return _windowsPerCategory.back();
    }
    
    WindowData* findDataForWindow (const ImGuiID& windowID)
    {
        for (auto& data : _windowsData)
            if (data->id() == windowID)
                return data.get();
        return nullptr;
    }
    
private:
    // Might get accessed as read-only by other threads.
    // If you want to modify it, you need to grab a lock.
    struct
    {
        std::mutex lock;
        ImGuiStorage windowsByID;
    } concurrent;
    
private:
    std::vector<std::unique_ptr<Window>> _windows;
    std::vector<std::unique_ptr<WindowData>> _windowsData;
    std::vector<WindowCategory> _windowsPerCategory;
};

struct Context
{
    struct
    {
        std::mutex lock;
        std::vector<std::function<void(void)>> tasksForNextFrame;
        std::map<std::string, std::function<void(void)>> tasksToRepeatForEachFrame;
    } concurrentTasks;
        
    // Cache to avoid reallocating data on every frame.
    struct {
        std::vector<std::function<void(void)>> tasksToRun;
    } cache;
    
    WindowManager windowManager;
};

} // CVLog
} // ImGui

namespace ImGui
{
namespace CVLog
{

Context* g_Context = new Context();

const char* Window::name() const
{
    return imGuiData->name().c_str();
}

void SetPerFrameCallback(const char* callbackName,
                         const std::function<void(void)>& callback)
{
    std::lock_guard<std::mutex> _ (g_Context->concurrentTasks.lock);
    if (callback)
    {
        g_Context->concurrentTasks.tasksToRepeatForEachFrame[callbackName] = callback;
    }
    else
    {
        g_Context->concurrentTasks.tasksToRepeatForEachFrame.erase(callbackName);
    }
}

void SetWindowProperties(const char* windowName,
                         const char* categoryName, /* = nullptr for no change */
                         const char* helpString, /* = nullptr for no change */
                         int preferredWidth, /* -1 for no change */
                         int preferredHeight /* -1 for no change */)
{
    std::string windowNameCopy = windowName ? windowName : "";
    std::string categoryNameCopy = categoryName ? categoryName : "";
    std::string helpStringCopy = helpString ? helpString : "";
    
    RunOnceInImGuiThread([=]() {
        auto& winData = g_Context->windowManager.FindOrCreateDataForWindow(windowNameCopy.c_str());

        if (!categoryNameCopy.empty())
            g_Context->windowManager.SetWindowCategory(windowNameCopy.c_str(), categoryNameCopy.c_str());

        if (!helpStringCopy.empty())
            winData.helpString = helpStringCopy;
        
        if (preferredWidth > 0)
            winData.preferredSize.x = preferredWidth;
        
        if (preferredHeight > 0)
            winData.preferredSize.y = preferredHeight;
    });
}

void SetWindowPreRenderCallback(const char* windowName,
                                const char* callbackName,
                                const std::function<void(void)>& callback)
{
    std::string windowNameCopy = windowName;
    std::string callbackNameCopy = callbackName;
    RunOnceInImGuiThread([windowNameCopy,callbackNameCopy,callback]() {
        auto& winData = g_Context->windowManager.FindOrCreateDataForWindow(windowNameCopy.c_str());
        if (callback)
        {
            winData.preRenderCallbacks[callbackNameCopy] = callback;
        }
        else
        {
            winData.preRenderCallbacks.erase(callbackNameCopy);
        }
    });
}

void RunOnceInImGuiThread(const std::function<void(void)>& f)
{
    std::lock_guard<std::mutex> _ (g_Context->concurrentTasks.lock);
    g_Context->concurrentTasks.tasksForNextFrame.emplace_back(f);
}

Window* FindOrCreateWindow(const char* name, const std::function<Window*(void)>& createWindowFunc)
{
    Window* window = g_Context->windowManager.ConcurrentFindWindow(name);
    if (window)
        return window;
    
    auto* concreteWindow = createWindowFunc();
    g_Context->windowManager.AddWindow(name, std::unique_ptr<Window>(concreteWindow));
    return concreteWindow;
}

Window* FindWindow(const char* windowName)
{
    return g_Context->windowManager.ConcurrentFindWindow(windowName);
}

static void LoggerSettingsHandler_ClearAll(ImGuiContext* ctx, ImGuiSettingsHandler*)
{
    ImGuiContext& g = *ctx;
    for (int i = 0; i != g.Windows.Size; i++)
        g.Windows[i]->SettingsOffset = -1;
    g.SettingsWindows.clear();
}

static void* LoggerSettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name)
{
    WindowData& settings = g_Context->windowManager.FindOrCreateDataForWindow(name);
    return (void*)&settings;
}

static void LoggerSettingsHandler_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
{
    WindowData* settings = (WindowData*)entry;
    int i;
    if (sscanf(line, "Visible=%d", &i) == 1)
    {
        settings->isVisibleRef() = i;
    }
}

// Apply to existing windows (if any)
static void LoggerSettingsHandler_ApplyAll(ImGuiContext* ctx, ImGuiSettingsHandler*)
{
}

static void LoggerSettingsHandler_WriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    const auto& windowsData = g_Context->windowManager.windowsData();
    
    // Write to text buffer
    buf->reserve(buf->size() + (int)windowsData.size() * 6); // ballpark reserve
    for (const auto& winData : windowsData)
    {
        buf->appendf("[%s][%s]\n", handler->TypeName, winData->name().c_str());
        buf->appendf("Visible=%d\n", winData->isVisible());
        buf->append("\n");
    }
}

// Gui-thread only
void Initialize()
{
    // Add .ini handle for ImGuiWindow type
    {
        ImGuiSettingsHandler ini_handler;
        ini_handler.TypeName = "CvLogData";
        ini_handler.TypeHash = ImHashStr("CvLogData");
        ini_handler.ClearAllFn = LoggerSettingsHandler_ClearAll;
        ini_handler.ReadOpenFn = LoggerSettingsHandler_ReadOpen;
        ini_handler.ReadLineFn = LoggerSettingsHandler_ReadLine;
        ini_handler.ApplyAllFn = LoggerSettingsHandler_ApplyAll;
        ini_handler.WriteAllFn = LoggerSettingsHandler_WriteAll;
        ImGui::GetCurrentContext()->SettingsHandlers.push_back(ini_handler);
    }
}

// Gui thread only.
void Render()
{
    {
        std::lock_guard<std::mutex> _ (g_Context->concurrentTasks.lock);
        g_Context->cache.tasksToRun.insert (g_Context->cache.tasksToRun.begin(),
                                            g_Context->concurrentTasks.tasksForNextFrame.begin(),
                                            g_Context->concurrentTasks.tasksForNextFrame.end());
        g_Context->concurrentTasks.tasksForNextFrame.clear();
        
        for (const auto& it : g_Context->concurrentTasks.tasksToRepeatForEachFrame)
            g_Context->cache.tasksToRun.push_back(it.second);
    }
    
    for (auto& task : g_Context->cache.tasksToRun)
        task();
    
    g_Context->cache.tasksToRun.clear();
    
    g_Context->windowManager.Render();
}

} // CVLog
} // ImGui
