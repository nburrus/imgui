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

Window* FindWindow(const char* windowName)
{
    return g_Context->windowManager.ConcurrentFindWindow(windowName);
}

namespace GuiThread
{

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

void AddWindow(const char* windowName, std::unique_ptr<Window> window)
{
    g_Context->windowManager.AddWindow (windowName, std::move(window));
}

} // GuiThread

} // Logger
} // ImGui
