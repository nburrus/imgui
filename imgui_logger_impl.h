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

#include <OpenGL/gl3.h>

#include <mutex>

#define GL_SILENCE_DEPRECATION 1

namespace ImGui
{
namespace Logger
{

class Window
{
public:
    Window (const char* c_name)
    : name (c_name), id(ImHashStr(c_name)), isVisible(false)
    {}
    
    virtual ~Window ()
    {}
    
    virtual void Render() = 0;
    
public:
    ImGuiID id;
    std::string name;
    bool isVisible;
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
    } concurrentTasks;
    
    std::vector<std::unique_ptr<Window>> windows;
};

extern Context* g_Context;

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
    ImageWindow* imWindow = dynamic_cast<WindowType*>(window);
    IM_ASSERT (imWindow != nullptr);
    return imWindow;
}

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
    
    {
        std::lock_guard<std::mutex> _(g_Context->concurrentWindows.lock);
        g_Context->concurrentWindows.windowsByID.SetVoidPtr(id, window);
    }
    
    return concreteWindow;
}

}
}
