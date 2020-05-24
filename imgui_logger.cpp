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
    {
        std::lock_guard<std::mutex> _ (g_Context->concurrentTasks.lock);
        std::string windowNameCopy = windowName;
        g_Context->concurrentTasks.tasksForNextFrame.emplace_back([windowNameCopy,image](){
            ImageWindow* imWindow = FindOrCreateWindow<ImageWindow>(windowNameCopy.c_str());
            imWindow->UpdateImage(image);
        });
    }
}

void AddPlotValue(const char* window,
                  const char* group,
                  double yValue,
                  double xValue)
{
//    PlotWindow* plotWindow = GetOrCreateWindow<PlotWindow> (windowName);
//    plotWindow->addValue (window, group, yValue, xValue);
}

void Render()
{
    std::vector<std::function<void(void)>> tasksToRun;
    {
        std::lock_guard<std::mutex> _ (g_Context->concurrentTasks.lock);
        tasksToRun.swap (g_Context->concurrentTasks.tasksForNextFrame);
    }
    for (auto& task : tasksToRun)
        task();
    
    for (auto& window : g_Context->windows)
    {
        window->Render();
    }
}

} // Logger
} // ImGui
