#pragma once

#include <juce_opengl/juce_opengl.h>

#include "VisualAnalysis.h"

#include <functional>
#include <memory>

// Draws Kaleidobox Visuals on the graphics chip: the look (or two looks melting into each other),
// fed back into itself with a slow zoom and turn for the trails, then a soft colour split and edge.
// Runs on the OpenGL thread that JUCE gives the picture; the audio thread never waits for it.
namespace crate::visuals
{
class VisualsRenderer : public juce::OpenGLRenderer
{
public:
    VisualsRenderer (VisualTap&, VisualState&);
    ~VisualsRenderer() override;

    // The song's clock, set by the message thread (several times a second is plenty).
    void setClock (const Clock&);
    // Called (on the OpenGL thread) after each frame with what was drawn, for the controls to follow.
    std::function<void (const Frame&)> onFrame;
    // False when the graphics chip refused the shaders (the view says so instead of a black screen).
    bool working() const { return ! failed.load(); }

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // The shaders' source, for the tests (they're compiled on the chip at run time).
    static juce::String sceneShader();
    static juce::String feedbackShader();
    static juce::String outputShader();
    static juce::String vertexShader();

private:
    struct Gl;
    std::unique_ptr<Gl> gl;
    VisualAnalysis analysis;
    VisualState& state;
    juce::SpinLock clockLock;
    Clock clock;
    double lastMs = 0;
    int snapshotFrames = 0;
    std::atomic<bool> failed { false };
};
} // namespace crate::visuals
