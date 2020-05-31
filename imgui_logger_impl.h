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

#define GL_SILENCE_DEPRECATION 1

namespace ImGui
{
namespace Logger
{

class Window
{
public:
    Window (const char* c_name)
    : name (c_name), id(ImHashStr(c_name))
    {}
    
    virtual ~Window ()
    {}
    
    virtual void Render() = 0;
    
public:
    ImGuiID id;
    std::string name;
};

class ImageWindow : public Window
{
public:
    ImageWindow (const char* name) : Window (name)
    {}
    
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
        
        if (ImGui::Begin(name.c_str()))
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
    PlotWindow (const char* name) : Window (name)
    {}
    
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
                
        if (ImGui::Begin(name.c_str()))
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

struct WindowProperties
{
    static const char* defaultCategoryName() { return "Unsorted"; }
    
    std::string name;
    std::string category = defaultCategoryName();
    ImGuiID id = 0;
    ImVec2 preferredSize = ImVec2(320,240);
    std::string helpString = "No help specified";
    bool isVisible = true;
    Window* window = nullptr;
    
    std::map<std::string, std::function<void(void)>> extraRenderCallbacks;
};

struct WindowCategory
{
    std::string name;
    std::vector<WindowProperties> windowProperties;
};

class WindowListManager
{
public:
    WindowProperties& AddWindow (Window* window, const std::string& categoryName)
    {
        auto& props = SetWindowCategory(window->name, categoryName);
        props.window = window;
        return props;
    }
    
    // Could take a single set with a Json to know which properties to update.
    WindowProperties& SetWindowCategory (const std::string& windowName, const std::string& category)
    {
        auto& props = FindOrCreatePropertiesForWindow(windowName, category);
        if (props.category == category)
            return props;
        
        return ChangeWindowCategory(props, category);
    }
    
    WindowProperties& SetWindowPreferredSize (const std::string& windowName, const ImVec2& preferredSize)
    {
        auto& props = FindOrCreatePropertiesForWindow(windowName, WindowProperties::defaultCategoryName());
        props.preferredSize = preferredSize;
        return props;
    }
    
    WindowProperties& SetWindowHelpString (const std::string& windowName, const std::string& helpString)
    {
        auto& props = FindOrCreatePropertiesForWindow(windowName, WindowProperties::defaultCategoryName());
        props.helpString = helpString;
        return props;
    }
    
    WindowProperties& FindOrCreatePropertiesForWindow (const std::string& windowName,
                                                       const std::string& categoryNameIfNeedToCreateIt)
    {
        ImGuiID windowID = ImHashStr(windowName.c_str());
        WindowProperties* props = findPropertiesForWindow(windowID);
        if (props != nullptr)
            return *props;
        return createDefaultPropertiesForWindow(windowName, categoryNameIfNeedToCreateIt);
    }
    
    void Render()
    {
        auto& IO = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(200, IO.DisplaySize.y), ImGuiCond_Once);
        if (ImGui::Begin("Window List"))
        {
            if (ImGui::Button("Hide All"))
            {
                for (auto& cat : _windowsPerCategory)
                    for (auto& props : cat.windowProperties)
                        props.isVisible = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Show All"))
            {
                for (auto& cat : _windowsPerCategory)
                for (auto& props : cat.windowProperties)
                    props.isVisible = true;
            }
            
            for (auto& cat : _windowsPerCategory)
            {
                ImGui::BulletText("%s", cat.name.c_str());
                for (auto& props : cat.windowProperties)
                {
                    ImGui::Checkbox(props.name.c_str(), &props.isVisible);
                }
            }
        }
        ImGui::End();
        
        for (auto& cat : _windowsPerCategory)
        {
            for (auto& props : cat.windowProperties)
            {
                if (props.window && props.isVisible)
                {
                    if (!props.extraRenderCallbacks.empty())
                    {
                        if (ImGui::Begin(props.name.c_str()))
                        {
                            for (const auto& it : props.extraRenderCallbacks)
                                it.second();
                        }
                        ImGui::End();
                    }

                    props.window->Render();
                }
            }
        }
    }
    
private:
    WindowProperties& createDefaultPropertiesForWindow (const std::string& windowName, const std::string& categoryName)
    {
        auto& cat = findOrCreateCategory(categoryName);
        cat.windowProperties.emplace_back();
        auto& properties = cat.windowProperties.back();
        properties.name = windowName;
        properties.id = ImHashStr(windowName.c_str());
        properties.category = categoryName;
        return properties;
    }
    
    WindowCategory& findOrCreateCategory(const std::string& categoryName)
    {
        for (auto& cat : _windowsPerCategory)
            if (cat.name == categoryName)
                return cat;
        _windowsPerCategory.push_back(WindowCategory());
        _windowsPerCategory.back().name = categoryName;
        return _windowsPerCategory.back();
    }
    
    WindowProperties* findPropertiesForWindow (const ImGuiID& windowID)
    {
        for (auto& cat : _windowsPerCategory)
            for (auto& prop : cat.windowProperties)
                if (prop.id == windowID)
                    return &prop;
        return nullptr;
    }
        
    WindowProperties& ChangeWindowCategory(WindowProperties& properties, const std::string& newCategoryName)
    {
        auto& oldCat = findOrCreateCategory(properties.category);
        
        auto oldCatIt = std::find_if(oldCat.windowProperties.end(), oldCat.windowProperties.end(), [&](WindowProperties& p) {
            return &properties == &p;
        });
        
        auto newProperties = properties;
        newProperties.category = newCategoryName;
        oldCat.windowProperties.erase(oldCatIt);
        
        auto& newCat = findOrCreateCategory(newCategoryName);
        newCat.windowProperties.push_back (newProperties);
        return newCat.windowProperties.back();
    }
    
private:
    std::vector<WindowCategory> _windowsPerCategory;
};

struct Context
{
    // Might get accessed as read-only by other threads.
    // If you want to modify it, you need to grab a lock.
    struct
    {
        std::mutex lock;
        ImGuiStorage windowsByID;
    } concurrentWindows;
        
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
    
    std::vector<std::unique_ptr<Window>> windows;
    WindowListManager windowListManager;
};

extern Context* g_Context;

void RunOnceInImGuiThread(const std::function<void(void)>& f)
{
    std::lock_guard<std::mutex> _ (g_Context->concurrentTasks.lock);
    g_Context->concurrentTasks.tasksForNextFrame.emplace_back(f);
}

inline Window* FindWindowById(ImGuiID id)
{
    void* window = g_Context->concurrentWindows.windowsByID.GetVoidPtr(id);
    return reinterpret_cast<Window*>(window);
}

template <class WindowType>
WindowType* FindWindow (const char* name)
{
    ImGuiID id = ImHashStr(name);
    Window* window = FindWindowById(id);
    
    if (window == nullptr)
        return nullptr;
    
    // Non-unique hash? Should be extremely rare! Rename your window if that somehow happens.
    IM_ASSERT (window->name == name);
    WindowType* imWindow = dynamic_cast<WindowType*>(window);
    IM_ASSERT (imWindow != nullptr);
    return imWindow;
}

// Only from the ImGui thread.
template <class WindowType>
WindowType* FindOrCreateWindow (const char* name)
{
    WindowType* concreteWindow = FindWindow<WindowType>(name);
    if (concreteWindow)
        return concreteWindow;
    
    ImGuiID id = ImHashStr(name);
    concreteWindow = new WindowType(name);
    Window* window = concreteWindow;
    g_Context->windows.emplace_back(std::unique_ptr<Window>(window));
    g_Context->windowListManager.AddWindow(window, WindowProperties::defaultCategoryName());
    
    {
        std::lock_guard<std::mutex> _(g_Context->concurrentWindows.lock);
        g_Context->concurrentWindows.windowsByID.SetVoidPtr(id, window);
    }
    
    return concreteWindow;
}

}
}
