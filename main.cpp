#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>
#include <mutex>
#include "miniaudio.h"

const int W = 320, H = 200;
std::vector<uint32_t> fb(W *H); // 0xAABBGGRR on little-endian
float zbuf[W];
float pitch;
float sensitivity = 200;
float damage_timer = 0.0f;
float playerhealth = 100;
int kills = 0;
float survive_time = 0.0f;
float best_time = 0.0f;
int best_player = 0;
int player_num = 1;
bool new_best = false;
bool gameover = false;
bool mouse_first = true;
float intensity = 8.0f;
float grenade_cooldown = 0.0f;
const float GRAVITY = 60.0f;
const float GRENADE_R = 2.0f;
bool boss_spawned = false;
// ---------- audio (all sounds are synthesized at startup) ----------
const int SR = 44100;
const float TAU = 6.2831853f;
float availablegrenades = 3.0f;
int bosscount = 0;

enum SoundId
{
    SND_SHOT,
    SND_HIT,
    SND_DEATH,
    SND_HURT,
    SND_GAMEOVER,
    SND_COUNT
};


struct grenade{

    float gx, gy;
    float time = 3.0f;
    float vx, vy;
    float fues;
    bool active = true;
    float z = 6.0f;
    float vz = 0.0f;
};
std::vector<grenade> grenades;
struct Sound
{
    std::vector<float> samples;
};
struct Voice
{
    const Sound *snd = nullptr;
    size_t pos = 0;
    float vol = 1.0f;
};

Sound sounds[SND_COUNT];
Voice voices[16];
std::mutex audio_mutex;
ma_device audio_device;
bool audio_ok = false;

float noise()
{
    static uint32_t s = 12345;
    s = s * 1664525u + 1013904223u;
    return (s >> 8) / 8388608.0f - 1.0f;
}

Sound makeShot()
{
    Sound s;
    int n = (int)(0.45f * SR);
    s.samples.resize(n);
    float lp = 0.0f, phase = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float t = (float)i / SR;
        lp += 0.3f * (noise() - lp);
        phase += TAU * (45.0f + 140.0f * std::exp(-t * 25.0f)) / SR;
        float boom = std::sin(phase) * std::exp(-t * 9.0f);
        float crack = noise() * std::exp(-t * 45.0f);
        float v = 0.9f * boom + 0.9f * lp * std::exp(-t * 10.0f) + 0.5f * crack;
        s.samples[i] = std::clamp(v, -1.0f, 1.0f);
    }
    return s;
}

Sound makeHit()
{
    Sound s;
    int n = (int)(0.12f * SR);
    s.samples.resize(n);
    float lp = 0.0f, phase = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float t = (float)i / SR;
        lp += 0.35f * (noise() - lp);
        phase += TAU * (90.0f + 220.0f * std::exp(-t * 18.0f)) / SR;
        float v = 0.6f * std::sin(phase) * std::exp(-t * 25.0f) + 0.6f * lp * std::exp(-t * 30.0f);
        s.samples[i] = std::clamp(v, -1.0f, 1.0f);
    }
    return s;
}

Sound makeDeath()
{
    Sound s;
    int n = (int)(0.6f * SR);
    s.samples.resize(n);
    float lp = 0.0f, phase = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float t = (float)i / SR;
        float f = (230.0f * std::exp(-t * 3.5f) + 40.0f) * (1.0f + 0.1f * std::sin(TAU * 28.0f * t));
        phase += TAU * f / SR;
        float cycle = phase / TAU;
        float saw = (cycle - std::floor(cycle)) * 2.0f - 1.0f;
        lp += 0.18f * (saw - lp);
        float env = std::min(1.0f, t * 200.0f) * std::exp(-t * 5.0f);
        float v = 1.2f * lp * env + 0.15f * noise() * std::exp(-t * 20.0f);
        s.samples[i] = std::clamp(v, -1.0f, 1.0f);
    }
    return s;
}

Sound makeHurt()
{
    Sound s;
    int n = (int)(0.25f * SR);
    s.samples.resize(n);
    float lp = 0.0f, phase = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float t = (float)i / SR;
        lp += 0.2f * (noise() - lp);
        phase += TAU * (55.0f + 70.0f * std::exp(-t * 20.0f)) / SR;
        float v = std::sin(phase) * std::exp(-t * 12.0f) + 0.5f * lp * std::exp(-t * 25.0f);
        s.samples[i] = std::clamp(v, -1.0f, 1.0f);
    }
    return s;
}

Sound makeGameOver()
{
    Sound s;
    int n = (int)(1.8f * SR);
    s.samples.resize(n);
    const float freq[3] = {196.0f, 164.8f, 130.8f};
    const float start[3] = {0.0f, 0.4f, 0.8f};
    for (int i = 0; i < n; i++)
    {
        float t = (float)i / SR;
        float v = 0.0f;
        for (int k = 0; k < 3; k++)
        {
            if (t < start[k])
                continue;
            float tt = t - start[k];
            float env = std::exp(-tt * (k == 2 ? 2.2f : 5.0f)) * std::min(1.0f, tt * 100.0f);
            float ph = TAU * freq[k] * tt;
            v += env * (std::sin(ph) + 0.5f * std::sin(2.0f * ph) + 0.25f * std::sin(3.0f * ph));
        }
        s.samples[i] = std::clamp(v * 0.35f, -1.0f, 1.0f);
    }
    return s;
}

void audioCallback(ma_device *, void *out, const void *, ma_uint32 frames)
{
    float *o = (float *)out;
    std::lock_guard<std::mutex> lock(audio_mutex);
    for (ma_uint32 i = 0; i < frames; i++)
    {
        float mix = 0.0f;
        for (auto &v : voices)
        {
            if (!v.snd)
                continue;
            mix += v.snd->samples[v.pos] * v.vol;
            if (++v.pos >= v.snd->samples.size())
                v.snd = nullptr;
        }
        o[i] = std::clamp(mix * 0.6f, -1.0f, 1.0f);
    }
}

void playSound(SoundId id, float vol = 1.0f)
{
    if (!audio_ok)
        return;
    std::lock_guard<std::mutex> lock(audio_mutex);
    for (auto &v : voices)
    {
        if (!v.snd)
        {
            v.snd = &sounds[id];
            v.pos = 0;
            v.vol = vol;
            return;
        }
    }
}

void initAudio()
{
    sounds[SND_SHOT] = makeShot();
    sounds[SND_HIT] = makeHit();
    sounds[SND_DEATH] = makeDeath();
    sounds[SND_HURT] = makeHurt();
    sounds[SND_GAMEOVER] = makeGameOver();

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = SR;
    cfg.dataCallback = audioCallback;
    if (ma_device_init(nullptr, &cfg, &audio_device) != MA_SUCCESS)
    {
        std::cerr << "Audio init failed, running without sound\n";
        return;
    }
    if (ma_device_start(&audio_device) != MA_SUCCESS)
    {
        ma_device_uninit(&audio_device);
        std::cerr << "Audio start failed, running without sound\n";
        return;
    }
    audio_ok = true;
}

struct enemy
{
    float px;
    float py;
    float hitradius = 2.0f;
    int hp = 5; 
    bool alive = true;
    float hurt = 0.0f;
    float death = 0.0f;
    float attachcooldown = 0.0f;
    float angle = 0.0f;
    bool is_boss = false;
};
std::vector<enemy> enemies;

const float SHOT_TIME = 0.14f;
float shot_timer = 0.0f;
float spawn_countdown = 1.0f; // Spawns every 3 seconds
int shot_count = 0;
void putPixel(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= W || y >= H)
        return;
    fb[y * W + x] = c;
}

void framebuffer_size_callback(GLFWwindow *, int w, int h) { glViewport(0, 0, w, h); }
float fireray(float angle);
GLuint makeProgram()
{
    const char *vs = R"(#version 330 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
    const char *fs = R"(#version 330 core
in vec2 uv; out vec4 color; uniform sampler2D tex;
void main() { color = texture(tex, uv); })";

    auto compile = [](GLenum type, const char *src)
    {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            std::cerr << log;
        }
        return s;
    };
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    return p;
}

void drawline(int x0, int y0, int x1, int y1, uint32_t color)
{

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int xcurr = x0;
    int ycurr = y0;

    int xstep = (x0 < x1) ? 1 : -1;
    int ystep = (y0 < y1) ? 1 : -1;

    putPixel(xcurr, ycurr, color);
    if (abs(dy) <= abs(dx))
    {
        int pk = 2 * dy - dx;
        while (xcurr != x1)
        {
            if (pk < 0)
            {
                pk = pk + 2 * dy;
                xcurr += xstep;
                putPixel(xcurr, ycurr, color);
            }
            else
            {
                pk = pk + 2 * dy - 2 * dx;
                xcurr += xstep;
                ycurr += ystep;
                putPixel(xcurr, ycurr, color);
            }
        }
    }
    else
    {
        int pk = 2 * dx - dy;
        while (ycurr != y1)
        {
            if (pk < 0)
            {
                pk += 2 * dx;
            }
            else
            {
                pk += 2 * (dx - dy);
                xcurr += xstep;
            }
            ycurr += ystep;
            putPixel(xcurr, ycurr, color);
        }
    }
}
uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

const int MAP_W = 10, MAP_H = 10;
int map[MAP_H][MAP_W] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 1, 1, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1, 0, 1},
    {1, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 1, 1, 0, 1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

int blocksize = 16;

void fillRect(int x, int y, int w, int h, uint32_t color)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            putPixel(x + i, y + j, color);
}
void colorrect(int mapx, int mapy, uint32_t color)
{
    fillRect(mapx * blocksize, mapy * blocksize, blocksize, blocksize, color);
}
void rendermap()
{

    for (int y = 0; y < MAP_H; y++)
    {
        for (int x = 0; x < MAP_W; x++)
        {
            if (map[y][x] == 1)
            {
                colorrect(x, y, rgb(200, 200, 200));
            }
            else
            {
                colorrect(x, y, rgb(8, 22, 75));
            }
        }
    }
}

void drawplayer(float px, float py, float angle)
{
    float d = fireray(angle);
    int endx = px + cos(angle) * d;
    int endy = py + sin(angle) * d;
    fillRect(px - 2, py - 2, 5, 5, rgb(255, 0, 0));
    drawline(px, py, endx, endy, rgb(100, 199, 100));
}

float px = 1.5f * blocksize;
float py = 1.5f * blocksize;
float angle = 0.0f;
const float rotation_speed = 3.0f;
const float movespeed = 50.0f;

bool checkcollision(float nextx, float nexty);
void throwgrenade();
void shoot(float angle, std::vector<enemy> &enemies);
const float mouse_yaw_sens = 0.0025f;
const float mouse_pitch_sens = 0.4f;
const float max_pitch = H / 3.0f;

void handleInput(GLFWwindow *window, double dt)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    static double lastx = 0, lasty = 0;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (mouse_first)
    {
        lastx = mx;
        lasty = my;
        mouse_first = false;
    }

    angle += (mx - lastx) * mouse_yaw_sens;
    pitch -= (my - lasty) * mouse_pitch_sens;
    pitch = std::clamp(pitch, -max_pitch, max_pitch);
    lastx = mx;
    lasty = my;

    float stepx = cos(angle) * movespeed * dt;
    float stepy = sin(angle) * movespeed * dt;
    float leftstepy = cos(angle) * movespeed * dt;
    float leftstepx = -sin(angle) * movespeed * dt;
    float rightstepx = sin(angle) * movespeed * dt;
    float rightstepy = -cos(angle) * movespeed * dt;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS && checkcollision(px + stepx, py + stepy))
    {
        px += stepx;
        py += stepy;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS && checkcollision(px - stepx, py - stepy))
    {
        px -= stepx;
        py -= stepy;
    }

    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS && checkcollision(px + leftstepx, py + leftstepy))
    {
        px += leftstepx;
        py += leftstepy;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS && checkcollision(px + rightstepx, py + rightstepy))
    {
        px += rightstepx;
        py += rightstepy;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && shot_timer <= 0)
    {
        playSound(SND_SHOT);
        intensity = std::min(intensity + 2.0f, 18.0f);
        shoot(angle, enemies);
        shoot(angle + 0.12f, enemies);
        shoot(angle - 0.1f, enemies);
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS
        && grenade_cooldown <= 0 && availablegrenades >= 1.0f)
    {
        availablegrenades -= 1.0f;
        throwgrenade();
        grenade_cooldown = 1.0f;
    }
}
bool checkcollision(float nextx, float nexty)
{

    int mapx = nextx / blocksize;
    int mapy = nexty / blocksize;
    if (mapx < 0 || mapx >= MAP_W || mapy < 0 || mapy >= MAP_H)
        return false;
    if (map[mapy][mapx] == 1)
    {
        return false;
    };
    return true;
}

float fireray(float angle)
{
    float rx = px;
    float ry = py;
    while (checkcollision(rx, ry))
    {
        rx += cos(angle);
        ry += sin(angle);
    }

    return sqrt((rx - px) * (rx - px) + (ry - py) * (ry - py));
}
void triggerShotEffect()
{
    shot_timer = SHOT_TIME;
    shot_count++;
}

int recoilOffset()
{
    if (shot_timer <= 0)
        return 0;
    float t = shot_timer / SHOT_TIME;
    return -(int)(intensity * t * t);
}

void shoot(float angle, std::vector<enemy> &enemies)
{
    triggerShotEffect();
    float walldist = fireray(angle);
    float bestdist = walldist;
    enemy *best = nullptr;
    for (auto &e : enemies)
    {
        if (e.alive)
        {
            float enemydistance = sqrt(pow((e.px - px), 2) + pow((e.py - py), 2));
            if (enemydistance < walldist)
            {
                float enemyangle = atan2(e.py - py, e.px - px);
                float d = angle - enemyangle;
                float diff = std::fabs(atan2(sin(d), cos(d)));
                float threshold = e.hitradius / enemydistance;
                if (diff <= threshold && enemydistance < bestdist)
                {
                    best = &e;
                    bestdist = enemydistance;
                }
            }
        }
    }
    if (best)
    {
        best->hurt = 0.12f;
        best->hp--;
        if (best->hp <= 0)
        {
            best->alive = false;
            kills++;
            playSound(SND_DEATH, 0.9f);
        }
        else
        {
            playSound(SND_HIT, 0.6f);
        }
    }
}

const float PI = 3.14159265f;
float fov = PI / 3.0f; // 60 degrees

void drawMuzzleFlash()
{
    if (shot_timer <= 0)
        return;
    float t = shot_timer / SHOT_TIME;
    if (t < 0.35f)
        return;

    float s = (t - 0.35f) / 0.65f;
    int cx = W / 2;
    int cy = 122 + recoilOffset();
    float len = 6.0f + 16.0f * s;
    float rot = shot_count * 0.7f;

    for (int i = 0; i < 8; i++)
    {
        float a = rot + i * PI / 4.0f;
        float l = (i % 2 == 0) ? len : len * 0.5f;
        drawline(cx, cy, (int)(cx + cos(a) * l), (int)(cy + sin(a) * l), rgb(255, 140, 30));
        drawline(cx, cy, (int)(cx + cos(a) * l * 0.6f), (int)(cy + sin(a) * l * 0.6f), rgb(255, 220, 80));
    }
    fillRect(cx - 2, cy - 2, 5, 5, rgb(255, 255, 220));
}

void render3d()
{

    float fov = PI / 3.0f; // 60 degrees
    float half_fov = fov / 2.0f;
    float horizon = (H / 2) + pitch;
    for (int x = 0; x < W; x++)
    {
        float ray_angle = (angle - half_fov) + ((float)x / (float)W) * fov;
        // hat el masafa
        float distance = fireray(ray_angle);
        zbuf[x] = distance;
        // e7seb height
        float wallheigh = (blocksize / distance) * 200.0f;

        int start = horizon - ((int)wallheigh / 2);
        int end = horizon + ((int)wallheigh / 2);
        start = std::max(0, start);
        end = std::min(H - 1, end);

        float wf = std::clamp(1.1f - distance / 200.0f, 0.3f, 1.0f);

        // sa2f
        for (int y = 0; y < start; y++)
        {
            float k = std::clamp((horizon - y) / (H / 2.0f), 0.0f, 1.0f);
            putPixel(x, y, rgb(46 - 34 * k, 44 - 32 * k, 70 - 42 * k));
        }

        // ard
        for (int y = start; y < end; y++)
        {
            putPixel(x, y, rgb(255, 255, 255));
        }

        for (int y = start; y <= end; y++)
        {
            putPixel(x, y, rgb(78 * wf, 96 * wf, 150 * wf));
        }

        // floor
        for (int y = end + 1; y < H; y++)
        {
            float k = std::clamp((y - horizon) / (H / 2.0f), 0.0f, 1.0f);
            putPixel(x, y, rgb(34 + 52 * k, 30 + 44 * k, 30 + 34 * k));
        }
    }
}

uint32_t shade(int r, int g, int b, float f)
{
    return rgb(std::clamp((int)(r * f), 0, 255),
               std::clamp((int)(g * f), 0, 255),
               std::clamp((int)(b * f), 0, 255));
}

uint32_t adjust(uint32_t c, float mul, float white)
{
    int ch[3] = {(int)(c & 0xFF), (int)((c >> 8) & 0xFF), (int)((c >> 16) & 0xFF)};
    for (int &v : ch)
    {
        float f = std::clamp(v * mul, 0.0f, 255.0f);
        v = (int)(f + (255.0f - f) * white);
    }
    return rgb(ch[0], ch[1], ch[2]);
}

// u: 0..1 left to right, v: 0..1 top to bottom. returns 0 where the sprite is transparent
uint32_t imp(float u, float v)
{
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return 0;
    float cx = u - 0.5f;
    float ax = std::fabs(cx);
    float lit = 1.0f - cx * 0.5f; // light comes from the left

    // eyes
    float ex = ax - 0.075f, ey = v - 0.19f;
    if ((ex * ex) / 0.0016f + (ey * ey) / 0.0009f <= 1.0f)
        return rgb(255, 235, 70);

    // angry brows
    float browy = 0.15f + (0.115f - ax) * 0.2f;
    if (ax > 0.03f && ax < 0.115f && std::fabs(v - browy) < 0.014f)
        return rgb(45, 6, 6);

    // mouth with teeth
    if (ax < 0.085f && v > 0.255f && v < 0.29f)
    {
        int t = (int)std::floor((cx + 0.085f) * 70.0f);
        if (v < 0.27f && t % 2 == 0)
            return rgb(235, 228, 195);
        return rgb(35, 0, 0);
    }

    // head
    float hx = cx / 0.17f, hy = (v - 0.2f) / 0.11f;
    if (hx * hx + hy * hy <= 1.0f)
        return shade(170, 48, 34, lit);

    // horns
    if (v < 0.15f)
    {
        float hornx = 0.21f - v * 0.4f;
        float hw = 0.006f + 0.032f * (v / 0.15f);
        if (std::fabs(ax - hornx) < hw)
            return shade(228, 218, 176, lit);
    }

    // hands and claws
    float hdx = ax - 0.29f, hdy = v - 0.64f;
    if (hdx * hdx + hdy * hdy <= 0.0028f)
        return shade(105, 24, 18, lit);
    if (v > 0.67f && v < 0.73f && std::fabs(hdx) < 0.04f && ((int)std::floor(ax * 90.0f)) % 2 == 0)
        return shade(228, 218, 176, lit);

    // arms
    if (v >= 0.34f && v <= 0.64f)
    {
        float axc = 0.255f + (v - 0.34f) * 0.12f;
        if (std::fabs(ax - axc) < 0.055f)
            return shade(150, 40, 30, lit * 0.92f);
    }

    // belt and buckle
    if (v >= 0.585f && v <= 0.655f && ax < 0.16f)
    {
        if (ax < 0.03f && v > 0.6f && v < 0.64f)
            return shade(230, 185, 50, lit);
        return shade(78, 50, 32, lit);
    }

    // torso with ribs
    if (v >= 0.29f && v < 0.62f)
    {
        float hw = (v < 0.33f) ? 0.08f + (v - 0.29f) * 3.0f : 0.2f - (v - 0.33f) * 0.2f;
        if (ax < hw)
        {
            if (v > 0.37f && v < 0.55f && ax < 0.1f && std::fmod(v, 0.045f) < 0.012f)
                return shade(85, 16, 12, lit);
            return shade(138, 38, 28, lit);
        }
    }

    // legs and hooves
    if (v >= 0.62f)
    {
        if (v > 0.93f && ax > 0.03f && ax < 0.2f)
            return shade(48, 34, 30, lit);
        if (std::fabs(ax - 0.095f) < 0.06f)
            return shade(122, 32, 24, lit);
    }
    return 0;
}

void drawEnemies()
{
    const float half_fov = fov / 2.0f;
    const double t = glfwGetTime();

    std::vector<std::pair<float, int>> order;
    for (int i = 0; i < (int)enemies.size(); i++)
    {
        float dx = enemies[i].px - px, dy = enemies[i].py - py;
        order.push_back({std::sqrt(dx * dx + dy * dy), i});
    }
    std::sort(order.begin(), order.end(),
              [](const auto &a, const auto &b)
              { return a.first > b.first; });

    for (const auto &o : order)
    {
        enemy &e = enemies[o.second];
        float dist = o.first;
        if (dist < 4.0f)
            continue;

        float dx = e.px - px, dy = e.py - py;
        float d = std::atan2(dy, dx) - angle;
        float delta = std::atan2(std::sin(d), std::cos(d));
        if (std::fabs(delta) > 1.4f)
            continue;

        float vscale = 1.0f, wscale = 1.0f;
        if (e.alive)
        {
            vscale += 0.015f * std::sin(t * 3.0 + o.second * 1.7);
        }
        else
        {
            vscale = std::max(0.1f, 1.0f - e.death / 0.3f);
            wscale = 1.0f + (1.0f - vscale) * 0.5f;
        }

        float s = 200.0f / dist;
        float scale = e.is_boss ? 3.0f : 1.0f;
        float wpx = 10.5f * s * wscale * scale;
        float hpx = 13.0f * s * vscale * scale;
        float feet = (H / 2.0f + pitch) + 8.0f * s;
        float top = feet - hpx;
        float left = (delta + half_fov) / fov * W - wpx * 0.5f;
        float du = 1.0f / wpx, dv = 1.0f / hpx;

        float fog = 1.0f - std::min(dist / 260.0f, 0.45f);
        if (!e.alive)
            fog *= 0.55f;
        float white = (e.hurt > 0.0f) ? 0.65f : 0.0f;

        int xa = std::max(0, (int)std::floor(left));
        int xb = std::min(W - 1, (int)std::ceil(left + wpx));
        int ya = std::max(0, (int)std::floor(top));
        int yb = std::min(H - 1, (int)std::ceil(feet));

        for (int x = xa; x <= xb; x++)
        {
            if (dist >= zbuf[x])
                continue;
            float u = (x + 0.5f - left) / wpx;
            for (int y = ya; y <= yb; y++)
            {
                float v = (y + 0.5f - top) / hpx;
                uint32_t c = imp(u, v);
                if (!c)
                    continue;
                if (!imp(u - du, v) || !imp(u + du, v) || !imp(u, v - dv) || !imp(u, v + dv))
                    c = rgb(25, 4, 4);
                putPixel(x, y, adjust(c, fog, white));
            }
        }
    }
}

void drawgrenade(grenade gn){
    const float half_fov = fov/2.0f;
    float dx = gn.gx - px, dy = gn.gy - py;
    float dist = sqrt(dx * dx + dy * dy);
    float d = atan2(dy, dx) - angle;
    float r_world = 2.0f;
    float delta = atan2(sin(d), cos(d));
    if (delta < -half_fov || delta > half_fov || dist < 4.0f)
        return;
    float screenx = (delta + half_fov) / fov * W;
    float s = 200.0f / dist;
    float screeny = (H/2 + pitch) + (8 - gn.z) * s;
    int r = std::max(1, (int)(GRENADE_R * s));
    int cx = (int) screenx;
    int cy = (int) screeny;
    for (int oy = -r; oy <= r; oy ++){
        for (int ox = -r; ox <= r; ox ++){
            if (ox * ox + oy * oy <= r * r){
                int x = cx + ox;
                if (x < 0 || x >= W){
                    continue;
                }
                if (dist >= zbuf[x])
                    continue;
                 putPixel(x, cy + oy, rgb(160, 200, 40));
            }
        }
    }
}

void fillWedge(int y0, int y1, float kl, float kr, uint32_t c)
{
    const int vy = H / 2;
    for (int y = y0; y < y1; y++)
    {
        int xl = W / 2 + (int)(kl * (y - vy));
        int xr = W / 2 + (int)(kr * (y - vy));
        fillRect(xl, y, xr - xl + 1, 1, c);
        
    }
}

void drawWeapon(int yoff = 0)
{
    const int cx = W / 2;
    const int a = 126 + yoff;
    const int b = 172 + yoff;

    fillWedge(a, b, -0.34f, -0.25f, rgb(72, 77, 84));
    fillWedge(a, b, 0.25f, 0.34f, rgb(34, 37, 42));
    fillWedge(a, b, -0.25f, 0.25f, rgb(108, 114, 123));
    fillWedge(a, b, -0.04f, 0.04f, rgb(80, 85, 93));

    fillRect(cx - 9, a - 5, 19, 5, rgb(30, 32, 36));
    fillRect(cx - 9, a - 5, 19, 1, rgb(90, 95, 102));
    fillRect(cx - 3, a - 4, 7, 3, rgb(8, 8, 10));
    fillRect(cx - 1, a - 8, 3, 3, rgb(20, 20, 22));

    fillRect(cx - 14, b - 8, 5, 6, rgb(25, 27, 30));
    fillRect(cx + 10, b - 8, 5, 6, rgb(25, 27, 30));
    fillRect(cx - 14, b - 8, 5, 1, rgb(130, 136, 145));
    fillRect(cx + 10, b - 8, 5, 1, rgb(130, 136, 145));

    fillWedge(b, H, -0.32f, 0.32f, rgb(52, 56, 62));

    fillWedge(b + 5, H, -0.44f, 0.44f, rgb(196, 146, 116));
    fillWedge(b + 5, H, 0.30f, 0.44f, rgb(150, 105, 82));
    fillWedge(b + 5, b + 7, -0.44f, 0.44f, rgb(220, 170, 140));
}

void spawnenemies()
{
    int active_count = 0;
    for (const auto &e : enemies)
    {
        if (e.alive)
            active_count++;
    }
    if (active_count >= 30)
    {
        return;
    }
    int start_i = rand() % MAP_H;
    int start_j = rand() % MAP_W;
    for (int i = 0; i < MAP_H; i++)
    {
        for (int j = 0; j < MAP_W; j++)
        {
            int r = (i + start_i) % MAP_H;
            int c = (j + start_j) % MAP_W;
            if (map[r][c] == 0)
            {

                float enx = (c + 0.5f) * blocksize;
                float eny = (r + 0.5f) * blocksize;
                float dist_to_player = std::sqrt(std::pow(enx - px, 2) + std::pow(eny - py, 2));
                if (dist_to_player > 64.0f)
                {
                    enemy newenemy;
                    newenemy.px = enx;
                    newenemy.py = eny;

                    newenemy.hp = 5;
                    newenemy.alive = true;

                    enemies.push_back(newenemy);
                    return;
                }
            }
        }
    }
}

void spawnBoss() {
    int start_i = rand() % MAP_H;
    int start_j = rand() % MAP_W;
    for (int i = 0; i < MAP_H; i++) {
        for (int j = 0; j < MAP_W; j++) {
            int r = (i + start_i) % MAP_H;
            int c = (j + start_j) % MAP_W;
            if (map[r][c] == 0) {
                float enx = (c + 0.5f) * blocksize;
                float eny = (r + 0.5f) * blocksize;
                float dist_to_player = std::sqrt(std::pow(enx - px, 2) + std::pow(eny - py, 2));
                if (dist_to_player > 80.0f) {
                    enemy boss;
                    boss.px = enx;
                    boss.py = eny;
                    boss.hp = 30;           // Massive health pool
                    boss.hitradius = 4.0f;  // Easier to hit, wider collision
                    boss.is_boss = true;
                    boss.alive = true;
                    enemies.push_back(boss);
                    std::cout << "A BOSS HAS SPAWNED!\n";
                    return;
                }
            }
        }
    }
}

bool haslineofsight(float ex, float ey, float targetx, float targety)
{

    float dx = targetx - ex;
    float dy = targety - ey;
    float angle = atan2(dy, dx);
    float dist = sqrt(dx * dx + dy * dy);
    float rx = ex;
    float ry = ey;
    float step = 5.0f;
    for (float i = 0; i < dist; i += step)
    {
        rx += cos(angle) * step;
        ry += sin(angle) * step;

        int mapx = (int)rx / blocksize;
        int mapy = (int)ry / blocksize;

        if (mapx < 0 || mapx >= MAP_W || mapy < 0 || mapy >= MAP_H || map[mapy][mapx] == 1)
            return false;
    }
    return true;
}

void moveenemies(enemy& e, double dt)
{


            float searchspeed = 20.0f;
         
            float rx = e.px;
            float ry = e.py;
            rx += cos(e.angle) * searchspeed * dt;
            ry += sin(e.angle) * searchspeed * dt;

            int mapx = (int)rx / blocksize;
            int mapy = (int)ry / blocksize;

            if (mapx < 0 || mapx >= MAP_W || mapy < 0 || mapy >= MAP_H || map[mapy][mapx] == 1)
            {
                e.angle += static_cast<float>(rand()) / RAND_MAX * 2.0f * PI;
            }
            else
            {
                e.px = rx;
                e.py = ry;
            }
            
        }
    

void updateEnemies(double dt)
{

    for (auto &e : enemies)
    {
        float enemy_speed = (e.is_boss) ? 60.0f : 30.0f;

        if (!e.alive)
            continue;

        // Always decrement cooldown if active, but don't skip movement
        if (e.attachcooldown > 0.0f)
        {
            e.attachcooldown -= dt;
        }

        float dx = px - e.px;
        float dy = py - e.py;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < 200.0f && haslineofsight(e.px, e.py, px, py))
        {
            float dirx = dx / dist;
            float diry = dy / dist;

            float next_x = e.px + dirx * enemy_speed * dt;
            float next_y = e.py + diry * enemy_speed * dt;

            int mapx = (int)(next_x / blocksize);
            int mapy = (int)(next_y / blocksize);

            if (mapy >= 0 && mapy < MAP_H && mapx >= 0 && mapx < MAP_W && map[mapy][mapx] == 0)
            {
                e.px = next_x;
                e.py = next_y;
            }

            // Use 'dist' instead of origin calculation, and check cooldown safely
            if (dist < 18.0f && e.attachcooldown <= 0.0f)
            {
                float attachcooldown = (e.is_boss) ? 0.5f : 1.0f; 
                e.attachcooldown = attachcooldown; // 1-second attack interval
                playerhealth -= 15;
                damage_timer = 0.25f;
                playSound(SND_HURT);
                std::cout << "Ouch! Player Health: " << playerhealth << "\n";
            }
        }
        else
        {
            moveenemies(e, dt);
        }
    }
}
void drawDamageFlash()
{
    if (damage_timer <= 0.0f)
        return;
    float t = damage_timer / 0.25f; // Intensity factor (0 to 1)

    // Tint the entire framebuffer red for a classic hit flash
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            uint32_t c = fb[y * W + x];
            uint8_t r = c & 0xFF;
            uint8_t g = (c >> 8) & 0xFF;
            uint8_t b = (c >> 16) & 0xFF;

            // Boost red, dim green/blue
            r = std::min(255, (int)(r + 120 * t));
            g = (int)(g * (1.0f - 0.6f * t));
            b = (int)(b * (1.0f - 0.6f * t));

            fb[y * W + x] = rgb(r, g, b);
        }
    }
}

// 3x5 bitmap font, 15 chars per glyph: five rows of three pixels
const char *glyph(char c)
{
    switch (c)
    {
    case '0': return "111" "101" "101" "101" "111";
    case '1': return "010" "110" "010" "010" "111";
    case '2': return "111" "001" "111" "100" "111";
    case '3': return "111" "001" "111" "001" "111";
    case '4': return "101" "101" "111" "001" "001";
    case '5': return "111" "100" "111" "001" "111";
    case '6': return "111" "100" "111" "101" "111";
    case '7': return "111" "001" "001" "001" "001";
    case '8': return "111" "101" "111" "101" "111";
    case '9': return "111" "101" "111" "001" "111";
    case '.': return "000" "000" "000" "000" "010";
    case 'A': return "010" "101" "111" "101" "101";
    case 'B': return "110" "101" "110" "101" "110";
    case 'E': return "111" "100" "110" "100" "111";
    case 'G': return "011" "100" "101" "101" "011";
    case 'H': return "101" "101" "111" "101" "101";
    case 'I': return "111" "010" "010" "010" "111";
    case 'K': return "101" "101" "110" "101" "101";
    case 'L': return "100" "100" "100" "100" "111";
    case 'M': return "101" "111" "111" "101" "101";
    case 'N': return "110" "101" "101" "101" "101";
    case 'O': return "111" "101" "101" "101" "111";
    case 'P': return "111" "101" "111" "100" "100";
    case 'R': return "110" "101" "110" "101" "101";
    case 'S': return "011" "100" "010" "001" "110";
    case 'T': return "111" "010" "010" "010" "010";
    case 'V': return "101" "101" "101" "101" "010";
    case 'W': return "101" "101" "111" "111" "101";
    case 'Y': return "101" "101" "010" "010" "010";
    default: return nullptr;
    }
}

void drawChar(int x, int y, char c, int scale, uint32_t color)
{
    const char *g = glyph(c);
    if (!g)
        return;
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 3; k++)
            if (g[r * 3 + k] == '1')
                fillRect(x + k * scale, y + r * scale, scale, scale, color);
}

void drawText(int x, int y, const std::string &s, int scale, uint32_t color)
{
    for (int i = 0; i < (int)s.size(); i++)
        drawChar(x + i * 4 * scale, y, s[i], scale, color);
}

int textWidth(const std::string &s, int scale)
{
    return s.empty() ? 0 : (int)s.size() * 4 * scale - scale;
}

void drawTextShadow(int x, int y, const std::string &s, int scale, uint32_t color)
{
    int o = std::max(1, scale / 2);
    drawText(x + o, y + o, s, scale, rgb(0, 0, 0));
    drawText(x, y, s, scale, color);
}

void drawCentered(int y, const std::string &s, int scale, uint32_t color)
{
    drawTextShadow((W - textWidth(s, scale)) / 2, y, s, scale, color);
}

void drawHUD()
{
    uint32_t white = rgb(235, 235, 235);
    int hp = std::clamp((int)playerhealth, 0, 100);
    uint32_t barcol = hp > 50 ? rgb(70, 210, 90) : (hp > 25 ? rgb(235, 190, 40) : rgb(225, 55, 45));

    drawTextShadow(4, 4, "HP", 2, white);
    fillRect(22, 4, 52, 10, rgb(0, 0, 0));
    fillRect(23, 5, 50, 8, rgb(55, 20, 20));
    fillRect(23, 5, hp / 2, 8, barcol);
    drawTextShadow(80, 4, std::to_string(hp), 2, white);

    std::string k = "KILLS " + std::to_string(kills);
    drawTextShadow(W - 4 - textWidth(k, 2), 4, k, 2, white);

    std::string t = "TIME " + std::to_string((int)survive_time);
    drawTextShadow((W - textWidth(t, 2)) / 2, 4, t, 2, white);
}

void drawGameOver()
{
    for (auto &c : fb)
    {
        int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
        c = rgb(r * 0.3f + 55, g * 0.3f, b * 0.3f);
    }

    uint32_t white = rgb(235, 235, 235);
    uint32_t gold = rgb(255, 210, 60);
    char buf[48];

    drawCentered(22, "GAME OVER", 4, rgb(230, 40, 35));
    drawCentered(66, "PLAYER " + std::to_string(player_num), 2, white);
    snprintf(buf, sizeof(buf), "TIME %.1f", survive_time);
    drawCentered(88, buf, 2, white);
    drawCentered(106, "KILLS " + std::to_string(kills), 2, white);

    if (new_best)
        drawCentered(132, "NEW BEST", 2, gold);
    else
    {
        snprintf(buf, sizeof(buf), "BEST %.1f PLAYER %d", best_time, best_player);
        drawCentered(132, buf, 2, gold);
    }

    if ((int)(glfwGetTime() * 2) % 2 == 0)
        drawCentered(168, "PRESS ENTER", 2, white);
}

void throwgrenade() {
    grenade gn;
    float sx = px + cos(angle) * 6.0f;
    float sy = py + sin(angle) * 6.0f;
    if (checkcollision(sx, sy)) { gn.gx = sx; gn.gy = sy; }
    else { gn.gx = px; gn.gy = py; }
    float speed = 100.0f;
    gn.vx = cos(angle) * speed;
    gn.vy = sin(angle) * speed;
    gn.vz = 25.0f + pitch * 0.3f;
    gn.fues = 2.0f;
    grenades.push_back(gn);
}

void boom(float x, float y) {
    float blast_radius = 40.0f; 
    float disttoplayer = sqrt((px - x) * (px - x) + (py - y) * (py - y));
    if (disttoplayer < blast_radius) {
        playerhealth -= 20.0f;
        damage_timer = 0.25f;
        playSound(SND_HURT);
        drawDamageFlash();
    }
    float r_world = 2.0f;
    for (auto& e : enemies) {
        if (!e.alive) continue;

        float dx = e.px - x;
        float dy = e.py - y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < blast_radius) {
            e.hurt = 0.2f;
            e.hp -= 5; 
            if (e.hp <= 0) {
                e.alive = false;
                kills++;
            }
        }
    }
    

}

void updategrenade(double dt){
    availablegrenades += dt * 0.2f;
    availablegrenades = std::min(availablegrenades, 3.0f);


    for (auto& gn : grenades){
        gn.fues -= dt;
        gn.vz -= GRAVITY * dt;
        gn.z += gn.vz * dt;

        bool rolling = false;
        if (gn.z <= GRENADE_R){
            gn.z = GRENADE_R;
            if (gn.vz < 0){
                if (-gn.vz < 10.0f){
                    gn.vz = 0;
                    rolling = true;
                } else {
                    gn.vz = -gn.vz * 0.5f;
                    gn.vx *= 0.7f;
                    gn.vy *= 0.7f;
                }
            }
        }
        if (gn.z > 15.0f){
            gn.z = 15.0f;
            if (gn.vz > 0)
                gn.vz = -gn.vz * 0.5f;
        }

        if (rolling){
            float f = std::max(0.0f, 1.0f - 3.0f * (float)dt);
            gn.vx *= f;
            gn.vy *= f;
        }

        float nextx = gn.gx + gn.vx * dt;
        if (checkcollision(nextx, gn.gy))
            gn.gx = nextx;
        else
            gn.vx = -gn.vx * 0.5f;

        float nexty = gn.gy + gn.vy * dt;
        if (checkcollision(gn.gx, nexty))
            gn.gy = nexty;
        else
            gn.vy = -gn.vy * 0.5f;

        if (gn.fues <= 0){
            boom(gn.gx, gn.gy);
            gn.active = false;
        }
    }
    grenades.erase(
        std::remove_if(grenades.begin(), grenades.end(), [](const grenade& g) { return !g.active; }),
        grenades.end()
    );
}

void restartGame()
{
    playerhealth = 100.0f;
    enemies.clear();
    px = 1.5f * blocksize;
    py = 1.5f * blocksize;
    angle = 0.0f;
    pitch = 0.0f;
    kills = 0;
    survive_time = 0.0f;
    spawn_countdown = 0.3f;
    damage_timer = 0.0f;
    shot_timer = 0.0f;
    intensity = 8.0f;
    new_best = false;
    gameover = false;
    mouse_first = true;
    player_num++;
    grenade_cooldown = 0.0f;
    grenades.clear();
    availablegrenades = 3.0f;
    boss_spawned = false;
    bosscount = 0;
}

int main()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(960, 600, "doom-but-shit", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "GLEW failed\n";
        return -1;
    }

    GLuint prog = makeProgram();
    GLuint vao;
    glGenVertexArrays(1, &vao);
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    initAudio();

    double last = glfwGetTime();
    enemy en1 = {88, 24};
    enemies.push_back(en1);

    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        double dt = now - last;
        last = now;

        glfwPollEvents();

        if (!gameover)
        {
            // ---------- UPDATE (input, movement) ----------
            handleInput(window, dt);
            updateEnemies(dt);
            updategrenade(dt);
            survive_time += dt;
            intensity = std::max(static_cast<float>(8.0f), intensity - static_cast<float>(dt * 6.0f));
            if (kills > 0 && kills % 20 == 0 && !boss_spawned) {
                bosscount ++;
                enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const enemy &e) {
                    return !e.is_boss;
                }), enemies.end());
                for (int i = 0; i < bosscount; i++) {
                    spawnBoss();
                }
                boss_spawned = true;
            }
            for (auto &e : enemies)
            {
                if (e.hurt > 0)
                    e.hurt -= dt;
                if (!e.alive && e.death < 1.0f)
                    e.death += dt;
            }
            enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                             [&](const enemy &e)
                             { 
                                 if (!e.alive && e.death >= 1.0f) {
                                     if (e.is_boss) boss_spawned = false; // Reset flag when boss despawns/dies
                                     return true;
                                 }
                                 return false; 
                             }),
              enemies.end());

            spawn_countdown -= dt;
            if (spawn_countdown <= 0.0f)
            {
                if (!boss_spawned || (boss_spawned && kills % 20 != 0)) {
                    spawnenemies();
                }
                spawn_countdown = 1.5f;
            }
            if (playerhealth <= 0.0f)
            {
                gameover = true;
                playSound(SND_GAMEOVER);
                if (survive_time > best_time)
                {
                    best_time = survive_time;
                    best_player = player_num;
                    new_best = true;
                }
            }

        }
        else
        {
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);
            if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS)
                restartGame();
        }

        if (shot_timer > 0)
            shot_timer -= dt;
        if (damage_timer > 0)
            damage_timer -= dt;

        if (grenade_cooldown > 0)
            grenade_cooldown -= dt;

        // ---------- RENDER (write into fb) ----------
        std::fill(fb.begin(), fb.end(), 0xFF202020);
        render3d();
        drawEnemies();
        for (auto& gn : grenades){
            drawgrenade(gn);
        }
        drawWeapon(recoilOffset());
        drawMuzzleFlash();
        drawDamageFlash();
        drawHUD();
        if (gameover)
            drawGameOver();
        // ---------- present ----------
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fb.data());
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glfwSwapBuffers(window);
    }
    if (audio_ok)
        ma_device_uninit(&audio_device);
    glfwTerminate();
}