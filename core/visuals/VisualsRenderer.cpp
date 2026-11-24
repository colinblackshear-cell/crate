#include "VisualsRenderer.h"

using namespace juce::gl;

namespace crate::visuals
{
namespace
{
const char* kCommon = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v;
uniform vec2 uRes;
uniform float uT, uBass, uMid, uHigh, uBeat, uBars, uAmt;
uniform vec3 uPal[6];
uniform float uLv[6];
#define PI 3.14159265
mat2 rot (float a) { float c = cos (a), s = sin (a); return mat2 (c, -s, s, c); }
// Walks round the six colours; the ones sounding most take up more of the way.
vec3 pal (float t)
{
    t = fract (t) * 6.0;
    vec3 c = vec3 (0.0);
    float W = 0.0;
    for (int k = 0; k < 6; k++)
    {
        float d = abs (mod (t - float (k) + 3.0, 6.0) - 3.0);
        float w = smoothstep (1.0, 0.0, d) * (0.3 + uLv[k]);
        c += uPal[k] * w;
        W += w;
    }
    return c / max (W, 0.001);
}
float hash (vec2 p) { return fract (sin (dot (p, vec2 (127.1, 311.7))) * 43758.5453); }
)";

const char* kScene = R"(
uniform sampler2D uWave;
uniform sampler2D uSpec;
uniform int uA;
uniform int uB;
uniform float uMix;
float wv (float x) { return texture2D (uWave, vec2 (fract (x), 0.5)).r - 0.5; }
float sp (float x) { return texture2D (uSpec, vec2 (clamp (x, 0.0, 1.0), 0.5)).r; }
vec3 kaleido (vec2 p)
{
    float n = 6.0 + 2.0 * floor (mod (uBars * 0.5, 4.0));
    float r = length (p), a = atan (p.y, p.x) + uT * 0.05;
    float seg = 2.0 * PI / n;
    a = mod (a, seg);
    a = abs (a - seg * 0.5);
    vec2 q = r * vec2 (cos (a), sin (a));
    q = rot (uT * 0.12) * q;
    q *= 1.0 + 0.35 * uBass * uAmt;
    for (int i = 0; i < 3; i++)
        q += 0.25 * vec2 (sin (q.y * 3.1 + uT * 0.7), cos (q.x * 2.7 - uT * 0.6)) * (0.5 + uMid * uAmt);
    float f = sin (q.x * 7.0 + uT) * sin (q.y * 7.0 - uT * 0.8);
    float line = smoothstep (0.12 + 0.1 * uHigh, 0.0, abs (f));
    vec3 c = pal (r * 0.6 + f * 0.15 - uT * 0.04) * (0.25 + line * 1.1);
    c += pal (r - uT * 0.1 + 0.5) * exp (-abs (r - (1.0 - uBeat) * 1.2) * 18.0) * uBeat * 1.3;
    return c * smoothstep (1.6, 0.2, r);
}
vec3 tunnel (vec2 p)
{
    float r = length (p) + 0.001, a = atan (p.y, p.x);
    float z = 0.35 / r + uT * (0.5 + 0.4 * uAmt) + uBars * 2.0;
    float s = sin (a * 6.0 + z * 2.0 + sin (uT * 0.3) * 3.0);
    float rings = smoothstep (0.0, 0.12, abs (fract (z * 1.5) - 0.5) - 0.38 + 0.2 * uBeat);
    float m = smoothstep (0.1, 0.0, abs (s) - 0.1 - uMid * 0.25 * uAmt);
    vec3 c = pal (z * 0.08 + a / (2.0 * PI)) * (m * 0.7 + rings * (0.35 + uBeat * 1.1));
    c *= smoothstep (0.0, 0.35, r);
    c += pal (z * 0.1 + 0.3) * uHigh * 0.25 * step (0.985, hash (floor (vec2 (a * 30.0, z * 20.0))));
    return c;
}
vec3 liquid (vec2 p)
{
    p *= 1.3 - 0.3 * uBass * uAmt;
    float t = uT * 0.6;
    vec2 q = p;
    for (int i = 0; i < 4; i++)
        q += 0.45 * vec2 (sin (q.y * 1.7 + t + float (i)), cos (q.x * 1.5 - t * 1.1)) * (0.6 + uBass * uAmt);
    float val = sin (q.x * 2.0) + sin (q.y * 2.3) + sin (length (q) * 2.0 - t * 2.0);
    vec3 c = pal (val * 0.18 + t * 0.05);
    c *= 0.55 + 0.45 * sin (val * 5.0 + uT * 2.0 + uMid * 4.0);
    c += uBeat * 0.25 * pal (val * 0.2 + 0.5);
    return c;
}
vec3 bloom (vec2 p)
{
    float r = length (p), a = atan (p.y, p.x);
    vec3 c = vec3 (0.0);
    for (int i = 0; i < 4; i++)
    {
        float fi = float (i);
        float k = i == 0 ? 5.0 : i == 1 ? 8.0 : i == 2 ? 13.0 : 21.0;
        float lvl = i == 0 ? uLv[0] : i == 1 ? uLv[2] : i == 2 ? uLv[3] : uLv[4];
        vec3 tc = i == 0 ? uPal[0] : i == 1 ? uPal[2] : i == 2 ? uPal[3] : uPal[4];
        float base = 0.15 + fi * 0.17;
        float amp = (0.08 + 0.22 * min (lvl, 1.0) * uAmt) * (1.0 + uBeat * 0.3);
        float edge = base + amp * pow (abs (cos (a * k * 0.5 + uT * (fi - 1.5) * 0.3)), 2.0);
        float in1 = smoothstep (0.012, 0.0, r - edge) * smoothstep (base - 0.12, base, r);
        float rim = exp (-abs (r - edge) * 70.0);
        c = mix (c, tc * (0.3 + 0.45 * min (lvl, 1.0)), in1 * 0.8);
        c += tc * rim * (0.4 + 0.6 * min (lvl, 1.0));
    }
    float s = sp (abs (a) / PI);
    float halo = 0.82 + s * 0.25 * uAmt;
    c += pal (a / PI + uT * 0.05) * exp (-abs (r - halo) * 30.0) * s * 1.2;
    c += vec3 (1.0, 0.95, 0.85) * smoothstep (0.07, 0.0, r) * (0.6 + uBeat);
    return c;
}
vec3 trails (vec2 p)
{
    float r = length (p), a = atan (p.y, p.x);
    float rad = 0.32 + wv (a / (2.0 * PI) + 0.5) * (0.35 + 0.4 * uAmt) + uBass * 0.08;
    float g = exp (-abs (r - rad) * (90.0 - 60.0 * uHigh));
    float hl = p.y - wv (p.x * 0.4 + 0.5) * (0.6 * uAmt + 0.2);
    float g2 = exp (-abs (hl) * 120.0) * 0.5 * smoothstep (1.2, 0.3, abs (p.x));
    return pal (a / (2.0 * PI) + uT * 0.07) * g * 1.4 + pal (p.x * 0.3 - uT * 0.1 + 0.5) * g2 * (0.4 + uMid);
}
vec3 look (int id, vec2 p)
{
    if (id == 0) return kaleido (p);
    if (id == 1) return tunnel (p);
    if (id == 2) return liquid (p);
    if (id == 3) return bloom (p);
    return trails (p);
}
void main()
{
    vec2 p = (v - 0.5) * uRes / min (uRes.x, uRes.y) * 2.0;
    vec3 c = look (uB, p);
    if (uMix < 1.0)
        c = mix (look (uA, p), c, uMix);
    gl_FragColor = vec4 (c, 1.0);
}
)";

const char* kFeedback = R"(
uniform sampler2D uPrev;
uniform sampler2D uNew;
uniform float uDecay;
void main()
{
    vec2 asp = uRes / min (uRes.x, uRes.y);
    vec2 q = (v - 0.5) * asp;
    float zoom = 0.985 - 0.03 * uBeat * uAmt - 0.01 * uBass;
    q = rot (0.004 + 0.012 * uBass * uAmt) * q;
    q *= zoom;
    q += 0.003 * vec2 (sin (q.y * 9.0 + uT), cos (q.x * 9.0 - uT)) * uMid;
    vec3 prev = texture2D (uPrev, q / asp + 0.5).rgb;
    prev = mix (prev, prev.gbr, 0.015);
    vec3 n = texture2D (uNew, v).rgb;
    vec3 c = max (prev * uDecay - 0.004, 0.0) + n * (1.0 - uDecay * 0.55);
    gl_FragColor = vec4 (min (c, 1.0), 1.0);
}
)";

const char* kOutput = R"(
uniform sampler2D uFb;
void main()
{
    float ab = 0.002 + 0.006 * uHigh * uAmt + 0.004 * uBeat;
    vec2 d = (v - 0.5) * ab;
    vec3 c = vec3 (texture2D (uFb, v + d).r, texture2D (uFb, v).g, texture2D (uFb, v - d).b);
    c *= 1.0 + 0.25 * uBeat;
    c = 1.0 - exp (-c * 1.5);
    c *= smoothstep (1.25, 0.35, length (v - 0.5) * 1.6);
    c = pow (c, vec3 (0.92));
    gl_FragColor = vec4 (c, 1.0);
}
)";
} // namespace

juce::String VisualsRenderer::vertexShader()
{
    return "attribute vec2 p;\nvarying vec2 v;\nvoid main() { v = p * 0.5 + 0.5; gl_Position = vec4 (p, 0.0, 1.0); }\n";
}
juce::String VisualsRenderer::sceneShader() { return juce::String (kCommon) + kScene; }
juce::String VisualsRenderer::feedbackShader() { return juce::String (kCommon) + kFeedback; }
juce::String VisualsRenderer::outputShader() { return juce::String (kCommon) + kOutput; }

//==============================================================================
struct VisualsRenderer::Gl
{
    explicit Gl (juce::OpenGLContext& c) : context (c) {}
    juce::OpenGLContext& context;
    std::unique_ptr<juce::OpenGLShaderProgram> scene, feedback, output;
    GLuint vbo = 0, vao = 0, waveTex = 0, specTex = 0;
    juce::OpenGLFrameBuffer targets[3];   // two for the trails (back and forth), one for the look
    int w = 0, h = 0, ping = 0;
    std::array<std::uint8_t, 512 * 4> waveBytes {};
    std::array<std::uint8_t, 64 * 4> specBytes {};

    std::unique_ptr<juce::OpenGLShaderProgram> build (const juce::String& fragment)
    {
        auto p = std::make_unique<juce::OpenGLShaderProgram> (context);
        if (! p->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vertexShader()))
            || ! p->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fragment)))
        {
            juce::Logger::outputDebugString ("Visuals shader: " + p->getLastError());
            return nullptr;
        }
        glBindAttribLocation (p->getProgramID(), 0, "p");
        if (! p->link())
        {
            juce::Logger::outputDebugString ("Visuals shader link: " + p->getLastError());
            return nullptr;
        }
        return p;
    }

    static GLuint makeLineTexture (int width)
    {
        GLuint t = 0;
        glGenTextures (1, &t);
        glBindTexture (GL_TEXTURE_2D, t);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        return t;
    }

    bool create()
    {
        scene = build (sceneShader());
        feedback = build (feedbackShader());
        output = build (outputShader());
        if (scene == nullptr || feedback == nullptr || output == nullptr)
            return false;
        const GLfloat quad[] { -1, -1, 1, -1, -1, 1, 1, 1 };
        glGenVertexArrays (1, &vao);
        glBindVertexArray (vao);
        glGenBuffers (1, &vbo);
        glBindBuffer (GL_ARRAY_BUFFER, vbo);
        glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray (0);
        glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        waveTex = makeLineTexture (512);
        specTex = makeLineTexture (64);
        return true;
    }

    void release()
    {
        for (auto& t : targets)
            t.release();
        if (vbo != 0) glDeleteBuffers (1, &vbo);
        if (vao != 0) glDeleteVertexArrays (1, &vao);
        if (waveTex != 0) glDeleteTextures (1, &waveTex);
        if (specTex != 0) glDeleteTextures (1, &specTex);
        vbo = vao = waveTex = specTex = 0;
        scene.reset();
        feedback.reset();
        output.reset();
    }

    void resize (int width, int height)
    {
        if (width == w && height == h)
            return;
        w = width;
        h = height;
        // (Each target is let go before the next: a target remembers what was bound when it took over.)
        for (auto& t : targets)
        {
            t.initialise (context, w, h);
            t.makeCurrentAndClear();
            t.releaseAsRenderingTarget();
        }
    }

    static void common (juce::OpenGLShaderProgram& p, const Frame& f, float width, float height)
    {
        juce::OpenGLShaderProgram::Uniform (p, "uRes").set (width, height);
        juce::OpenGLShaderProgram::Uniform (p, "uT").set (f.time);
        juce::OpenGLShaderProgram::Uniform (p, "uBass").set (f.bass);
        juce::OpenGLShaderProgram::Uniform (p, "uMid").set (f.mid);
        juce::OpenGLShaderProgram::Uniform (p, "uHigh").set (juce::jmin (1.0f, f.high));
        juce::OpenGLShaderProgram::Uniform (p, "uBeat").set (f.beat);
        juce::OpenGLShaderProgram::Uniform (p, "uBars").set (f.bars);
        juce::OpenGLShaderProgram::Uniform (p, "uAmt").set (f.wild);
        if (const auto loc = glGetUniformLocation (p.getProgramID(), "uPal"); loc >= 0)
            glUniform3fv (loc, 6, f.palette.data());
        if (const auto loc = glGetUniformLocation (p.getProgramID(), "uLv"); loc >= 0)
            glUniform1fv (loc, 6, f.weights.data());
    }

    static void bindTexture (juce::OpenGLShaderProgram& p, const char* name, int unit, GLuint texture)
    {
        glActiveTexture ((GLenum) (GL_TEXTURE0 + unit));
        glBindTexture (GL_TEXTURE_2D, texture);
        juce::OpenGLShaderProgram::Uniform (p, name).set ((GLint) unit);
    }

    void draw (const Frame& f, int outW, int outH)
    {
        // The look and its trails at a lighter size (they're soft anyway); only the last pass is full size.
        const float scale = juce::jmin (0.7f, 1400.0f / (float) juce::jmax (1, outW));
        resize (juce::jmax (2, (int) ((float) outW * scale)), juce::jmax (2, (int) ((float) outH * scale)));

        for (int i = 0; i < 512; ++i)
            waveBytes[(size_t) i * 4] = f.wave[(size_t) i];
        for (int i = 0; i < 64; ++i)
            specBytes[(size_t) i * 4] = f.spectrum[(size_t) i];
        glBindTexture (GL_TEXTURE_2D, waveTex);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 512, 1, GL_RGBA, GL_UNSIGNED_BYTE, waveBytes.data());
        glBindTexture (GL_TEXTURE_2D, specTex);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 64, 1, GL_RGBA, GL_UNSIGNED_BYTE, specBytes.data());

        glBindVertexArray (vao);
        glDisable (GL_BLEND);
        glDisable (GL_DEPTH_TEST);

        // 1: the look (two of them, melting, while Drift or a pick changes it).
        targets[2].makeCurrentRenderingTarget();
        glViewport (0, 0, w, h);
        scene->use();
        common (*scene, f, (float) w, (float) h);
        bindTexture (*scene, "uWave", 0, waveTex);
        bindTexture (*scene, "uSpec", 1, specTex);
        juce::OpenGLShaderProgram::Uniform (*scene, "uA").set ((GLint) f.lookA);
        juce::OpenGLShaderProgram::Uniform (*scene, "uB").set ((GLint) f.lookB);
        juce::OpenGLShaderProgram::Uniform (*scene, "uMix").set (f.mix);
        glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
        targets[2].releaseAsRenderingTarget();

        // 2: the trails: the last picture, zoomed and turned a little, faded, with the look on top.
        auto& from = targets[ping];
        auto& to = targets[1 - ping];
        to.makeCurrentRenderingTarget();
        glViewport (0, 0, w, h);
        feedback->use();
        common (*feedback, f, (float) w, (float) h);
        bindTexture (*feedback, "uPrev", 0, from.getTextureID());
        bindTexture (*feedback, "uNew", 1, targets[2].getTextureID());
        juce::OpenGLShaderProgram::Uniform (*feedback, "uDecay").set (f.decay);
        glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
        to.releaseAsRenderingTarget();
        ping = 1 - ping;

        // 3: onto the screen.
        glViewport (0, 0, outW, outH);
        output->use();
        common (*output, f, (float) outW, (float) outH);
        bindTexture (*output, "uFb", 0, to.getTextureID());
        glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray (0);
        glActiveTexture (GL_TEXTURE0);
    }
};

//==============================================================================
VisualsRenderer::VisualsRenderer (VisualTap& t, VisualState& s) : analysis (t), state (s) {}
VisualsRenderer::~VisualsRenderer() = default;

void VisualsRenderer::setClock (const Clock& c)
{
    const juce::SpinLock::ScopedLockType l (clockLock);
    clock = c;
}

void VisualsRenderer::newOpenGLContextCreated()
{
    auto* context = juce::OpenGLContext::getCurrentContext();
    if (context == nullptr)
    {
        failed = true;
        return;
    }
    gl = std::make_unique<Gl> (*context);
    failed = ! gl->create();
    lastMs = juce::Time::getMillisecondCounterHiRes();
}

void VisualsRenderer::renderOpenGL()
{
    auto* context = juce::OpenGLContext::getCurrentContext();
    if (gl == nullptr || failed.load() || context == nullptr || context->getTargetComponent() == nullptr)
    {
        juce::OpenGLHelpers::clear (juce::Colours::black);
        return;
    }
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = (now - lastMs) / 1000.0;
    lastMs = now;
    Clock c;
    {
        const juce::SpinLock::ScopedLockType l (clockLock);
        c = clock;
    }
    const auto& f = analysis.step (dt, c, state);
    const auto scale = context->getRenderingScale();
    auto* target = context->getTargetComponent();
    gl->draw (f, juce::roundToInt (scale * target->getWidth()), juce::roundToInt (scale * target->getHeight()));
    if (onFrame)
        onFrame (f);
    // VISUALS_SNAPSHOT=/folder: every 2 seconds the picture goes to a PNG there (the look's name, then a count),
    // to check the looks on a computer whose screen grab can't see OpenGL (the Linux build).
    static const auto snapshotFolder = juce::SystemStats::getEnvironmentVariable ("VISUALS_SNAPSHOT", {});
    if (snapshotFolder.isNotEmpty() && ++snapshotFrames % 120 == 0)
    {
        const int w = juce::roundToInt (scale * target->getWidth()), h = juce::roundToInt (scale * target->getHeight());
        juce::HeapBlock<GLubyte> px ((size_t) (w * h * 4));
        glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.get());
        juce::Image img (juce::Image::RGB, w, h, false);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto* p = px.get() + ((size_t) (h - 1 - y) * (size_t) w + (size_t) x) * 4;
                img.setPixelAt (x, y, juce::Colour (p[0], p[1], p[2]));
            }
        juce::PNGImageFormat png;
        juce::FileOutputStream out (juce::File (snapshotFolder).getChildFile (juce::String (lookName (f.lookB)) + "-" + juce::String (snapshotFrames / 120) + ".png"));
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            png.writeImageToStream (img, out);
        }
    }
}

void VisualsRenderer::openGLContextClosing()
{
    if (gl != nullptr)
        gl->release();
    gl.reset();
}
} // namespace crate::visuals
