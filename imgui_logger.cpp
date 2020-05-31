//
//  ImguiLogger.cpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 23/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#include "imgui_logger.h"

#include "imgui_logger_impl.h"

namespace ImGui
{
namespace Logger
{

Context* g_Context = new Context();

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

void SetWindowRenderExtraCallback(const char* windowName,
                                  const char* callbackName,
                                  const std::function<void(void)>& callback)
{
    std::string windowNameCopy = windowName;
    std::string callbackNameCopy = callbackName;
    RunOnceInImGuiThread([windowNameCopy,callbackNameCopy,callback]() {
        auto& props = g_Context->windowListManager.FindOrCreatePropertiesForWindow(windowNameCopy, WindowProperties::defaultCategoryName());
        if (callback)
        {
            props.extraRenderCallbacks[callbackNameCopy] = callback;
        }
        else
        {
            props.extraRenderCallbacks.erase(callbackNameCopy);
        }
    });
}

void UpdateImage(const char* windowName,
                 const ImagePtr& image)
{
    ImageWindow* imWindow = FindWindow<ImageWindow> (windowName);
    
    // The window exists, just update the data.
    if (imWindow)
    {
        imWindow->UpdateImage (image);
        return;
    }
    
    // Need to create it, enqueue that in the list of tasks for the next frame;
    std::string windowNameCopy = windowName;
    RunOnceInImGuiThread([windowNameCopy,image](){
        ImageWindow* imWindow = FindOrCreateWindow<ImageWindow>(windowNameCopy.c_str());
        imWindow->UpdateImage(image);
    });
}

void AddPlotValue(const char* windowName,
                  const char* groupName,
                  double yValue,
                  double xValue)
{
    PlotWindow* plotWindow = FindWindow<PlotWindow> (windowName);
    
    // The window exists, just update the data.
    if (plotWindow)
    {
        plotWindow->AddPlotValue(groupName, yValue, xValue);
        return;
    }
    
    // Need to create it, enqueue that in the list of tasks for the next frame;
    {
        std::string windowNameCopy = windowName;
        std::string groupNameCopy = groupName;
        RunOnceInImGuiThread([windowNameCopy,groupNameCopy,xValue,yValue](){
            PlotWindow* plotWindow = FindOrCreateWindow<PlotWindow>(windowNameCopy.c_str());
            plotWindow->AddPlotValue(groupNameCopy.c_str(), yValue, xValue);
        });
    }
}

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
    
    g_Context->windowListManager.Render();
}

} // Logger
} // ImGui
