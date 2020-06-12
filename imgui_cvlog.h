//
//  ImguiLogger.hpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 23/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#pragma once

#include "imgui.h"

#include <functional>

namespace ImGui
{
namespace CVLog
{

/*!
 Call this once per ImGui context.
 Required to create the settings handler.
 
 - Thread safety: only from the ImGui Thread.
 */
void Initialize();

/*!
 Call this once per frame.
 
 - Thread safety: only from the ImGui thread.
 */
void Render();

/*!
Run arbitrary ImGui code for each frame (e.g add UI element).

- Thread safety: any thread.
*/
void SetPerFrameCallback(const char* callbackName,
                         const std::function<void(void)>& callback);

/*!
- Thread safety: any thread.
*/
void SetWindowPreRenderCallback(const char* windowName,
                                const char* callbackName,
                                const std::function<void(void)>& callback);

/*!
- Thread safety: any thread.
*/
void SetWindowProperties(const char* windowName,
                         const char* categoryName, /* = nullptr for no change */
                         const char* helpString, /* = nullptr for no change */
                         int preferredWidth = -1, /* -1 for no change */
                         int preferredHeight = -1 /* -1 for no change */);

/*!
- Thread safety: any thread.
*/
void RunOnceInImGuiThread(const std::function<void(void)>& f);

// API to implement custom window types

class WindowData;
class Window
{
public:
    virtual ~Window () {}
    virtual void Render() = 0;
    
    const char* name() const;
    bool isVisible() const;
    
public:
    // Only access it from the ImGui thread, unless otherwise specified.
    WindowData* imGuiData = nullptr; // will get filled once added to the context.
};

/*!
- Thread safety: any thread.
*/
bool WindowIsVisible(const char* windowName);

/*!
- Thread safety: any thread.
*/
Window* FindWindow(const char* windowName);

/*!
- Thread safety: any thread.
*/
template <class WindowType>
WindowType* FindWindow (const char* name) { return dynamic_cast<WindowType*>(FindWindow(name)); }

/*!
- Thread safety: only from the ImGui thread.
*/
Window* FindOrCreateWindow(const char* windowName, const std::function<Window*(void)>& createWindowFunc);

/*!
- Thread safety: only from the ImGui thread.
*/
template <class WindowType>
WindowType* FindOrCreateWindow (const char* windowName)
{
    Window* window = FindOrCreateWindow(windowName, []() {
        return (Window*)new WindowType();
    });
    WindowType* concreteWindow = dynamic_cast<WindowType*>(window);
    IM_ASSERT (concreteWindow != nullptr);
    return concreteWindow;
}

} // CVLog
} // ImGui
