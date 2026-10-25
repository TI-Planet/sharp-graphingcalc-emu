#pragma once

#include "el9950/emulator.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

class FrontendApp
{
public:
    FrontendApp();
    ~FrontendApp();
    bool initialize();
    bool load_rom(const std::filesystem::path& path);
    void frame();

    bool
    running() const { return running_; }

private:
    bool update_skin();
    void clear_inputs();
    void event(const SDL_Event& event);
    void render_ui();
    void render_toolbar();
    void render_calculator();
    void render_debugger();
    void update_lcd_texture();
    void set_pointer_key(std::int64_t pointer, float x, float y, bool down);
    std::optional<el9950::Key> key_at(float x, float y) const;
    void sync_key(el9950::Key key);
    void toggle_latched_key(el9950::Key key);
    void release_transient_inputs();
    void keyboard_event(SDL_Keycode code, bool down);
    void pulse(el9950::Key key);
    void save_state_file(const std::filesystem::path& path);
    bool load_state_file(const std::filesystem::path& path);
    std::filesystem::path default_state_path() const;

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* skin_texture_{};
    std::string skin_image_;
    SDL_Texture* lcd_texture_{};
    el9950::Emulator emulator_;
    bool running_{true};
    bool emulation_running_{true};
    bool turbo_{};
    bool show_debugger_{true};
    std::string status_{"Drop a raw 1 MiB ROM to begin"};
    std::filesystem::path rom_path_;
    std::filesystem::path state_path_;
    std::uint64_t last_frame_ns_{};
    double cycle_fraction_{};
    std::array<float, 2> skin_screen_pos_{};
    std::array<float, 2> skin_screen_size_{};
    std::unordered_map<std::int64_t, el9950::Key> pointer_keys_;
    std::array<bool, static_cast<std::size_t>(el9950::Key::count)> keyboard_keys_{};
    std::array<bool, static_cast<std::size_t>(el9950::Key::count)> latched_keys_{};
    std::array<std::uint64_t, static_cast<std::size_t>(el9950::Key::count)> key_flash_until_ns_{};
};
