//
//  ImguiLogger.hpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 23/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ImGui
{
namespace Logger
{

class WindowData;
class Window
{
public:
    virtual ~Window () {}
    virtual void Render() = 0;
    
public:
    // Only access it from the ImGui thread, unless otherwise specified.
    WindowData* imGuiData = nullptr; // will get filled once added to the context.
};

// These methods are only safe from the ImGuiThread.
namespace GuiThread
{

// Takes ownership.
void AddWindow(const char* windowName, std::unique_ptr<Window> window);

void Render();

} // GuiThread

Window* FindWindow(const char* windowName);

template <class WindowType>
WindowType* FindWindow (const char* name)
{
    return dynamic_cast<WindowType*>(FindWindow(name));
}

// Run arbitrary ImGui code for each frame (e.g add UI element).
void SetPerFrameCallback(const char* callbackName,
                         const std::function<void(void)>& callback);

void SetWindowPreRenderCallback(const char* windowName,
                                const char* callbackName,
                                const std::function<void(void)>& callback);

// Image

struct Image
{
    std::vector<uint8_t> data;
    int bytesPerRow;
    int width;
    int height;
};
using ImagePtr = std::shared_ptr<Image>;

void UpdateImage(const char* windowName,
                 const ImagePtr& image);

// Plot

void AddPlotValue(const char* windowName,
                  const char* groupName,
                  double yValue,
                  double xValue);


} // Logger
} // ImGui
