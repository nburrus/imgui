//
//  imgui_logger_impl.h
//  ImguiLogger
//
//  Created by Nicolas Burrus on 24/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#pragma once

#include "imgui.h"

#include "imgui_internal.h"

#include "implot.h"

#include <OpenGL/gl3.h>

#include <mutex>

#include <unordered_map>

#include <map>

#include <set>

#define GL_SILENCE_DEPRECATION 1

namespace ImGui
{
namespace Logger
{

class Window;

struct WindowData
{
    static const char* defaultCategoryName() { return "Default"; }

    std::string name;
    ImGuiID id = 0; // == ImHashStr(name)
    
    // Can be nullptr if no window was yet created, but properties were specified.
    Window* window = nullptr;
    
    std::string category = defaultCategoryName();
    
    ImVec2 preferredSize = ImVec2(320,240);
    std::string helpString = "No help specified";
    bool isVisible = true;
    
    struct
    {
        bool hasData = false;
        ImVec2 pos = ImVec2(0,0);
        ImVec2 size = ImVec2(0,0);
        ImGuiCond_ imGuiCond = ImGuiCond_Always;
    } layoutUpdateOnNextFrame;
        
    std::map<std::string, std::function<void(void)>> preRenderCallbacks;
};

class ImageWindow : public Window
{
public:
    void UpdateImage (const ImagePtr& newImage)
    {
        std::lock_guard<std::mutex> _ (concurrent.imageLock);
        concurrent.image = newImage;
    }
    
    void Render() override
    {
        ImagePtr imageToShow;
        
        {
            std::lock_guard<std::mutex> _ (concurrent.imageLock);
            imageToShow = concurrent.image;
        }
        
        if (!imageToShow)
            return;
        
        if (_textureID == 0)
        {
            // Create a OpenGL texture identifier
            glGenTextures(1, &_textureID);
            glBindTexture(GL_TEXTURE_2D, _textureID);

            // Setup filtering parameters for display
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        
        if (imageToShow->data.data() != _imageDataUploadedToTexture)
        {
            // Upload pixels into texture
            glBindTexture(GL_TEXTURE_2D, _textureID);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageToShow->width, imageToShow->height, 0, GL_RED, GL_UNSIGNED_BYTE, imageToShow->data.data());
            _imageDataUploadedToTexture = imageToShow->data.data();
        }
        
        if (ImGui::Begin(imGuiData->name.c_str()))
        {
            float aspectRatio = float(imageToShow->height) / imageToShow->width;
            ImVec2 wSize = ImGui::GetWindowSize();
            
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("Show a window scrolling");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            
            ImGui::BulletText("ImageWindow content");
            ImGui::BulletText("Width: %d", imageToShow->width);
            ImGui::BulletText("Height: %d", imageToShow->height);
            ImGui::Image((void*)(intptr_t)_textureID, ImVec2(wSize.x, wSize.x*aspectRatio));
        }
        ImGui::End();
    }
    
private:
    struct {
        std::mutex imageLock;
        ImagePtr image;
    } concurrent;
    
    GLuint _textureID = 0;
    uint8_t* _imageDataUploadedToTexture = nullptr;
};

class PlotWindow : public Window
{
public:
    void AddPlotValue(const char* groupName,
                      float yValue,
                      float xValue)
    {
        std::lock_guard<std::mutex> _ (concurrent.lock);
        ImGuiID groupId = ImHashStr(groupName);
        concurrent.dataSinceLastFrame.push_back({groupId,xValue,yValue});
        if (!concurrent.existingGroups.GetBool(groupId))
            concurrent.addedGroupsSinceLastFrame.push_back(groupName);
    }
    
    void Render() override
    {
        {
            std::lock_guard<std::mutex> _ (concurrent.lock);
            _cacheOfDataToAppend.swap (concurrent.dataSinceLastFrame);
            for (const auto& groupName : concurrent.addedGroupsSinceLastFrame)
            {
                ImGuiID groupId = ImHashStr(groupName.c_str());
                _groupData[groupId].name = groupName;
                concurrent.existingGroups.SetBool(groupId, true);
            }
        }
        
        for (const auto& it : _cacheOfDataToAppend)
        {
            auto& groupData = _groupData[it.group];
            groupData.xData.push_back(it.xValue);
            groupData.yData.push_back(it.yValue);
            
            if (groupData.xData.size() == 1)
            {
                groupData.xMin = groupData.xMax = it.xValue;
                groupData.yMin = groupData.yMax = it.yValue;
            }
            else
            {
                groupData.xMin = std::min(groupData.xMin, it.xValue);
                groupData.xMax = std::max(groupData.xMax, it.xValue);
                groupData.yMin = std::min(groupData.yMin, it.yValue);
                groupData.yMax = std::max(groupData.yMax, it.yValue);
            }
            
            _dataBounds.xMin = std::min(_dataBounds.xMin, groupData.xMin);
            _dataBounds.xMax = std::max(_dataBounds.xMax, groupData.xMax);
            _dataBounds.yMin = std::min(_dataBounds.yMin, groupData.yMin);
            _dataBounds.yMax = std::max(_dataBounds.yMax, groupData.yMax);
        }
        _cacheOfDataToAppend.clear();
        
        if (_groupData.empty())
            return;
                
        if (ImGui::Begin(imGuiData->name.c_str()))
        {
            ImPlot::SetNextPlotLimits(_dataBounds.xMin, _dataBounds.xMax, _dataBounds.yMin, _dataBounds.yMax, ImGuiCond_Always);
            if (ImPlot::BeginPlot("Line Plot", "x", "f(x)"))
            {
                for (const auto& it : _groupData)
                {
                    ImPlot::PlotLine(it.second.name.c_str(), it.second.xData.data(), it.second.yData.data(), (int)it.second.xData.size());
                }
                ImPlot::EndPlot();
            }
        }
        ImGui::End();
    }
    
private:
    struct GroupData
    {
        std::string name;
        std::vector<float> xData;
        std::vector<float> yData;
        float xMin = 0;
        float xMax = 1;
        float yMin = 0;
        float yMax = 1;
    };
    
    struct DataToAppend
    {
        ImGuiID group;
        float xValue;
        float yValue;
    };
    
    struct {
        std::mutex lock;
        std::vector<DataToAppend> dataSinceLastFrame;
        std::vector<std::string> addedGroupsSinceLastFrame;
        ImGuiStorage existingGroups;
    } concurrent;
    
    std::unordered_map<ImGuiID,GroupData> _groupData;
    std::vector<DataToAppend> _cacheOfDataToAppend;
    
    struct {
        float xMin = 0;
        float xMax = 1;
        float yMin = 0;
        float yMax = 1;
    } _dataBounds; // across all groups.
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
            concurrent.windowsByID.SetVoidPtr(data.id, window);
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
            return w1->imGuiData->name < w2->imGuiData->name;
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
                if (!winData->isVisible)
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
                ImGui::SetWindowFocus(winData->name.c_str());
                
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
            if (ImGui::Button("Hide All"))
            {
                for (auto& cat : _windowsPerCategory)
                    for (auto& winData : cat.windows)
                        winData->isVisible = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Show All"))
            {
                for (auto& cat : _windowsPerCategory)
                for (auto& winData : cat.windows)
                    winData->isVisible = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Tile Windows"))
            {
                TileAndScaleVisibleWindows();
            }
            
            for (auto& cat : _windowsPerCategory)
            {
                const bool showCat = ImGui::CollapsingHeader(cat.name.c_str(), ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen);
                
                // Messing around with a button to toggle all the windows in the category.
                //                ImGui::SameLine(GetWindowContentRegionMax().x - 30);
                //
                //                if (ImGui::Button("All"))
                //                {
                //                    for (auto& props : cat.windowProperties)
                //                        props.isVisible = true;
                //                }
                
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
                    
                    ImGui::Checkbox(winData->name.c_str(), &winData->isVisible);
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                        ImGui::TextUnformatted(winData->name.c_str());
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
            if (winData->window && winData->isVisible)
            {
                if (winData->layoutUpdateOnNextFrame.hasData)
                {
                    ImGui::SetNextWindowPos(winData->layoutUpdateOnNextFrame.pos, ImGuiCond_Always);
                    ImGui::SetNextWindowSize(winData->layoutUpdateOnNextFrame.size, ImGuiCond_Always);
                    ImGui::SetNextWindowCollapsed(false, winData->layoutUpdateOnNextFrame.imGuiCond);
                    winData->layoutUpdateOnNextFrame = {}; // reset it.
                    ImGui::MarkIniSettingsDirty();
                }
                    
                ImGui::Begin(winData->name.c_str(), &winData->isVisible);
                ImGui::End();
                
                if (!winData->preRenderCallbacks.empty())
                {
                    if (ImGui::Begin(winData->name.c_str()))
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
        IM_ASSERT (window->imGuiData->name == name);
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
        _windowsData.emplace_back(std::make_unique<WindowData>());
        auto* winData = _windowsData.back().get();
        winData->name = windowName;
        winData->id = ImHashStr(windowName);
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
            if (data->id == windowID)
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

extern Context* g_Context;

void RunOnceInImGuiThread(const std::function<void(void)>& f)
{
    std::lock_guard<std::mutex> _ (g_Context->concurrentTasks.lock);
    g_Context->concurrentTasks.tasksForNextFrame.emplace_back(f);
}

// Only from the ImGui thread.
template <class WindowType>
WindowType* FindOrCreateWindow (const char* name)
{
    Window* window = g_Context->windowManager.ConcurrentFindWindow(name);
    if (window)
    {
        WindowType* concreteWindow = dynamic_cast<WindowType*>(window);
        IM_ASSERT (concreteWindow != nullptr);
        return concreteWindow;
    }
    
    auto* concreteWindow = new WindowType();
    g_Context->windowManager.AddWindow(name, std::unique_ptr<Window>(concreteWindow));
    return concreteWindow;
}

}
}
