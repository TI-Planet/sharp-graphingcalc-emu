#include "app.hpp"

#include "el9950/constants.hpp"
#include "el9950/debugger.hpp"
#include "el9950/skin.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <stb_image.h>

namespace
{
    std::string hex24(std::uint32_t value) {
        std::ostringstream s;
        s << std::hex << std::uppercase << std::setfill('0') << std::setw(6) << (value & 0xffffff);
        return s.str();
    }
}

FrontendApp::FrontendApp() = default;

FrontendApp::~FrontendApp() {
    if (emulator_.has_rom() && !state_path_.empty())
        save_state_file(state_path_);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    if (ImGui::GetCurrentContext())
        ImGui::DestroyContext();
    if (lcd_texture_)
        SDL_DestroyTexture(lcd_texture_);
    if (skin_texture_)
        SDL_DestroyTexture(skin_texture_);
    if (renderer_)
        SDL_DestroyRenderer(renderer_);
    if (window_)
        SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool FrontendApp::initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return false;
    if (!SDL_CreateWindowAndRenderer("Sharp EL-99xx Emulator", 1440, 900,SDL_WINDOW_RESIZABLE, &window_, &renderer_))
        return false;
    SDL_SetWindowMinimumSize(window_, 960, 640);
    SDL_SetRenderVSync(renderer_, 1);
#ifndef __EMSCRIPTEN__
    if (!update_skin()) return false;
#endif
    lcd_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, el9950::kLcdWidth,
                                     el9950::kLcdHeight);
    SDL_SetTextureScaleMode(lcd_texture_, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(lcd_texture_,SDL_BLENDMODE_NONE);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10, 8);
    style.FramePadding = ImVec2(10, 6);
    style.CellPadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 7);
    style.ItemInnerSpacing = ImVec2(6, 5);
    style.ScrollbarSize = 12;
    style.WindowRounding = 8;
    style.ChildRounding = 7;
    style.FrameRounding = 6;
    style.PopupRounding = 7;
    style.TabRounding = 6;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.FrameBorderSize = 1;
    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.040f, 0.048f, 0.052f, 1);
    colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.064f, 0.068f, 1);
    colors[ImGuiCol_PopupBg] = ImVec4(0.065f, 0.075f, 0.080f, 0.99f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.22f, 0.23f, 1);
    colors[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.090f, 0.095f, 1);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.19f, 0.19f, 1);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.25f, 0.24f, 1);
    colors[ImGuiCol_Button] = ImVec4(0.075f, 0.095f, 0.10f, 1);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.09f, 0.25f, 0.24f, 1);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.36f, 0.33f, 1);
    colors[ImGuiCol_Header] = ImVec4(0.08f, 0.22f, 0.21f, 1);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.10f, 0.31f, 0.29f, 1);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.12f, 0.40f, 0.37f, 1);
    colors[ImGuiCol_Tab] = ImVec4(0.065f, 0.080f, 0.085f, 1);
    colors[ImGuiCol_TabHovered] = ImVec4(0.10f, 0.30f, 0.28f, 1);
    colors[ImGuiCol_TabSelected] = ImVec4(0.08f, 0.22f, 0.21f, 1);
    colors[ImGuiCol_CheckMark] = ImVec4(0.27f, 0.79f, 0.72f, 1);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.25f, 0.68f, 0.63f, 1);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.34f, 0.88f, 0.79f, 1);
    colors[ImGuiCol_Separator] = ImVec4(0.16f, 0.19f, 0.20f, 1);
    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);
    return true;
}

bool FrontendApp::update_skin() {
    const auto model = emulator_.has_rom() ? emulator_.rom().model() : el9950::Model::unknown;
    const auto skin = el9950::skin_for(model, emulator_.keyboard_side());
#ifdef __EMSCRIPTEN__
    // Downloads run outside the ImGui frame. Poll until bytes are in MEMFS;
    // returning here keeps the UI and emulation responsive while fetching.
    const int ready = EM_ASM_INT({ return Module.requestSkin(UTF8ToString($0)); }, skin.image.data());
    if (ready != 1) {
        skin_screen_size_ = {};
        return true;
    }
#endif
    if (skin_image_ == skin.image && skin_texture_) return true;
#ifdef EL9950_BUNDLE_SKIN
    const std::string directory = std::string(SDL_GetBasePath()) + "../Resources/";
#elif defined(EL9950_PORTABLE_SKIN)
    const std::string directory = std::string(SDL_GetBasePath()) + "assets/";
#else
    const std::string directory = "/assets/";
#endif
    int width = 0, height = 0, channels = 0;
    auto* pixels = stbi_load((directory + std::string(skin.image)).c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        status_ = "Cannot load skin: " + std::string(skin.image);
#ifdef __EMSCRIPTEN__
        EM_ASM({ Module.skinFailed(UTF8ToString($0)); }, skin.image.data());
#endif
        return false;
    }
    auto* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    const bool loaded = texture && SDL_UpdateTexture(texture, nullptr, pixels, width * 4) &&
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    stbi_image_free(pixels);
    if (!loaded) {
        if (texture) SDL_DestroyTexture(texture);
        status_ = "Cannot create skin texture: " + std::string(SDL_GetError());
#ifdef __EMSCRIPTEN__
        EM_ASM({ Module.skinFailed(UTF8ToString($0)); }, skin.image.data());
#endif
        return false;
    }
    if (skin_texture_) SDL_DestroyTexture(skin_texture_);
    skin_texture_ = texture;
    skin_image_ = skin.image;
    return true;
}

void FrontendApp::clear_inputs() {
    latched_keys_.fill(false);
    key_flash_until_ns_.fill(0);
    release_transient_inputs();
}

bool FrontendApp::load_rom(const std::filesystem::path& path)
try {
    if (emulator_.has_rom() && !state_path_.empty()) save_state_file(state_path_);
    emulator_.load_rom(path);
    clear_inputs();
    rom_path_ = path;
    state_path_ = default_state_path();
    const auto verified = emulator_.known_rom();
    const bool restored = std::filesystem::exists(state_path_) && load_state_file(state_path_);
    if (!restored) {
        // Advance the cold reset to its power-off HALT, issue a short ON press,
        // then stop at the first powered-on idle HALT with a live LCD.
        auto run_to_halt = [this](std::uint64_t limit) {
            for (std::uint64_t done = 0; done < limit && !emulator_.cpu_state().halted;) {
                const auto ran = emulator_.run_cycles(std::min<std::uint64_t>(4096, limit - done));
                if (!ran)
                    break;
                done += ran;
            }
        };
        run_to_halt(20'000'000);
        pulse(el9950::Key::on);
        run_to_halt(20'000'000);
        status_ = verified ? "ROM 1.20 verified" : "ROM loaded";
    }
    if (emulator_.rom().model() == el9950::Model::unknown)
        status_ = "Unknown 1 MiB ROM — using EL-9950 skin";
    else if (!restored)
        status_ = std::string(el9950::model_name(emulator_.rom().model())) + " ROM " + emulator_.rom().version();
    return update_skin();
} catch (const std::exception& e) {
    status_ = e.what();
    return false;
}

void FrontendApp::frame() {
#ifdef __EMSCRIPTEN__
    if (emulator_.has_rom()) update_skin();
#endif
    SDL_Event e;
    while (SDL_PollEvent(&e))
        event(e);
    const auto now = SDL_GetTicksNS();
    if (!last_frame_ns_)
        last_frame_ns_ = now;
    const double elapsed = std::min(0.1, double(now - last_frame_ns_) / 1'000'000'000.0);
    last_frame_ns_ = now;
    if (emulation_running_ && emulator_.has_rom()) {
        cycle_fraction_ += elapsed * el9950::kClockHz * (turbo_ ? 16.0 : 1.0);
        const auto budget = static_cast<std::uint64_t>(cycle_fraction_);
        cycle_fraction_ -= double(budget);
        emulator_.run_cycles(budget);
        if (emulator_.breakpoint_hit()) {
            emulation_running_ = false;
            status_ = "Breakpoint hit at 0x" + hex24(emulator_.cpu_state().pc);
        }
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    render_ui();
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer_, 10, 12, 13, 255);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
}

void FrontendApp::event(const SDL_Event& e) {
    ImGui_ImplSDL3_ProcessEvent(&e);
    if (e.type == SDL_EVENT_QUIT)
        running_ = false;
    else if (e.type == SDL_EVENT_DROP_FILE && e.drop.data)
        load_rom(e.drop.data);
    else if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && !(e.type == SDL_EVENT_KEY_DOWN && e.key.repeat))
        keyboard_event(e.key.key, e.type == SDL_EVENT_KEY_DOWN);
    else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        release_transient_inputs();
    else if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_UP) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        set_pointer_key(e.tfinger.fingerID, e.tfinger.x * w, e.tfinger.y * h, e.type == SDL_EVENT_FINGER_DOWN);
    }
}

void FrontendApp::keyboard_event(SDL_Keycode c, bool down) {
    using K = el9950::Key;
    K key = K::count;
    switch (c) {
        case SDLK_0:
            key = K::n0;
            break;
        case SDLK_1:
            key = K::n1;
            break;
        case SDLK_2:
            key = K::n2;
            break;
        case SDLK_3:
            key = K::n3;
            break;
        case SDLK_4:
            key = K::n4;
            break;
        case SDLK_5:
            key = K::n5;
            break;
        case SDLK_6:
            key = K::n6;
            break;
        case SDLK_7:
            key = K::n7;
            break;
        case SDLK_8:
            key = K::n8;
            break;
        case SDLK_9:
            key = K::n9;
            break;
        case SDLK_RETURN:
            key = K::enter;
            break;
        case SDLK_PERIOD:
            key = K::decimal;
            break;
        case SDLK_BACKSPACE:
            key = K::backspace;
            break;
        case SDLK_DELETE:
            key = K::del;
            break;
        case SDLK_UP:
            key = K::up;
            break;
        case SDLK_DOWN:
            key = K::down;
            break;
        case SDLK_LEFT:
            key = K::left;
            break;
        case SDLK_RIGHT:
            key = K::right;
            break;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            key = K::second;
            break;
        case SDLK_X:
            key = K::variable;
            break;
        case SDLK_ESCAPE:
            key = K::on;
            break;
        default:
            break;
    }
    if (key == K::on) {
        if (down)
            pulse(key);
        return;
    }
    if (key != K::count) {
        keyboard_keys_[static_cast<std::size_t>(key)] = down;
        sync_key(key);
    }
}

void FrontendApp::set_pointer_key(std::int64_t id, float x, float y, bool down) {
    if (!down) {
        if (const auto it = pointer_keys_.find(id); it != pointer_keys_.end()) {
            const auto key = it->second;
            pointer_keys_.erase(it);
            sync_key(key);
        }
        return;
    }
    if (const auto key = key_at(x, y)) {
        if (*key == el9950::Key::on) {
            pulse(*key);
            return;
        }
        pointer_keys_[id] = *key;
        sync_key(*key);
    }
}

std::optional<el9950::Key> FrontendApp::key_at(float x, float y) const {
    if (skin_screen_size_[0] <= 0 || skin_screen_size_[1] <= 0)
        return std::nullopt;
    const auto skin = el9950::skin_for(emulator_.has_rom() ? emulator_.rom().model() : el9950::Model::unknown,
                                      emulator_.keyboard_side());
    return el9950::skin_key_at(skin, (x - skin_screen_pos_[0]) * skin.width / skin_screen_size_[0],
                               (y - skin_screen_pos_[1]) * skin.height / skin_screen_size_[1]);
}

void FrontendApp::sync_key(el9950::Key key) {
    const auto index = static_cast<std::size_t>(key);
    bool pressed = latched_keys_[index] || keyboard_keys_[index];
    for (const auto& [pointer,pointer_key] : pointer_keys_) {
        (void)pointer;
        if (pointer_key == key) {
            pressed = true;
            break;
        }
    }
    emulator_.set_key(key, pressed);
}

void FrontendApp::toggle_latched_key(el9950::Key key) {
    const auto index = static_cast<std::size_t>(key);
    latched_keys_[index] = !latched_keys_[index];
    sync_key(key);
    status_ = std::string(el9950::Keyboard::name(key, emulator_.keyboard_side())) + (latched_keys_[index] ? " held — right-click again to release" : " released");
}

void FrontendApp::release_transient_inputs() {
    pointer_keys_.clear();
    keyboard_keys_.fill(false);
    for (std::size_t index = 0; index < keyboard_keys_.size(); ++index)
        sync_key(static_cast<el9950::Key>(index));
}

void FrontendApp::render_ui() {
    const auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("EL-9950 Emulator", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.050f, 0.058f, 0.061f, 1));
    if (ImGui::BeginChild("titlebar", ImVec2(0, 34), ImGuiChildFlags_Borders)) {
        const std::string title_text = emulator_.has_rom() ?
            "SHARP " + std::string(el9950::model_name(emulator_.rom().model())) + "  " + emulator_.rom().version() :
            "SHARP EL-99xx Emulator";
        const char* title = title_text.c_str();
        const float title_width = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX(std::max(10.f, (ImGui::GetContentRegionAvail().x - title_width) * 0.5f));
        ImGui::TextUnformatted(title);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    render_toolbar();
    if (ImGui::BeginChild("workspace", ImVec2(0, -31), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::BeginTable("workspace-table", show_debugger_ ? 2 : 1, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("calculator", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            if (show_debugger_)
                ImGui::TableSetupColumn("debugger", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            ImGui::TableNextColumn();
            render_calculator();
            if (show_debugger_) {
                ImGui::TableNextColumn();
                ImGui::BeginChild("debugger-panel", ImVec2(0, 0), ImGuiChildFlags_Borders);
                render_debugger();
                ImGui::EndChild();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("statusbar", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        ImGui::TextUnformatted(status_.c_str());
        const auto cycles_text = std::string("Cycles  ") + std::to_string(emulator_.cycles());
        ImGui::SameLine(
            std::max(ImGui::GetCursorPosX() + 20.f, ImGui::GetWindowWidth() - ImGui::CalcTextSize(cycles_text.c_str()).x - 18.f));
        ImGui::TextDisabled("%s", cycles_text.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}

void FrontendApp::render_toolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 7));
    ImGui::BeginChild("toolbar", ImVec2(0, 51), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
#ifdef __EMSCRIPTEN__
    if (ImGui::Button("Load ROM"))
        EM_ASM({document.getElementById('rom-picker').click();});
#else
    ImGui::TextDisabled("Drop ROM");
#endif
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button(emulator_.reset_contact_open() ? "Close reset contact" : "Reset contact")) {
        if (emulator_.reset_contact_open())
            emulator_.set_reset_contact(false);
        else {
            emulator_.set_reset_contact(true);
            emulator_.set_reset_contact(false);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Power"))
        pulse(el9950::Key::on);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button(emulation_running_ ? "Pause" : "Run"))
        emulation_running_ = !emulation_running_;
    ImGui::SameLine();
    ImGui::Checkbox("Turbo", &turbo_);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    char battery_label[32];
    std::snprintf(battery_label, sizeof(battery_label), "Battery  %u%%", unsigned(emulator_.operating_battery_percent()));
    if (ImGui::Button(battery_label))
        ImGui::OpenPopup("battery-settings");
    if (ImGui::BeginPopup("battery-settings")) {
        ImGui::SeparatorText("Power");
        int percent = emulator_.operating_battery_percent();
        ImGui::SetNextItemWidth(260);
        if (ImGui::SliderInt("Operating batteries", &percent, 0, 100, "%d%%")) {
            emulator_.set_operating_battery_percent(static_cast<std::uint8_t>(percent));
            status_ = "Operating battery level changed";
        }
        bool backup = emulator_.backup_battery_healthy();
        if (ImGui::Checkbox("Backup battery healthy", &backup)) {
            emulator_.set_backup_battery_healthy(backup);
            status_ = backup ? "Backup battery: healthy" : "Backup battery: depleted";
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Operating battery warnings are not yet emulated.");
        ImGui::TextDisabled("A reset may be required for a boot-time warning.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("Save state") && emulator_.has_rom())
        save_state_file(state_path_);
    ImGui::SameLine();
    if (ImGui::Button("Load state") && emulator_.has_rom())
        (void)load_state_file(state_path_);
    ImGui::SameLine();
    ImGui::Checkbox("Debugger", &show_debugger_);
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void FrontendApp::pulse(el9950::Key key) {
    const bool was_pressed = emulator_.keyboard().pressed(key);
    emulator_.set_key(key, true);
    emulator_.run_cycles(500'000);
    if (!was_pressed)
        emulator_.set_key(key, false);
    key_flash_until_ns_[static_cast<std::size_t>(key)] = SDL_GetTicksNS() + 140'000'000ULL;
}

void FrontendApp::render_calculator() {
#ifdef __EMSCRIPTEN__
    if (!emulator_.has_rom()) {
        skin_screen_size_ = {};
        ImGui::TextWrapped("Load a ROM to display its calculator and keyboard.");
        return;
    }
#endif
    if (emulator_.has_rom() && el9950::has_reversible_keyboard(emulator_.rom().model())) {
        int side = static_cast<int>(emulator_.keyboard_side());
        ImGui::SetNextItemWidth(190);
        if (ImGui::Combo("Keyboard", &side, "Advanced\0Basic\0")) {
            const auto previous = emulator_.keyboard_side();
            emulator_.set_keyboard_side(static_cast<el9950::KeyboardSide>(side));
            if (update_skin()) {
                clear_inputs();
                status_ = "Keyboard flipped; press a calculator key to detect the change";
            } else emulator_.set_keyboard_side(previous);
        }
    }
    const auto avail = ImGui::GetContentRegionAvail();
    const float height = std::max(100.f, avail.y - 4);
    const auto skin = el9950::skin_for(emulator_.has_rom() ? emulator_.rom().model() : el9950::Model::unknown,
                                      emulator_.keyboard_side());
#ifdef __EMSCRIPTEN__
    if (skin_image_ != skin.image) {
        skin_screen_size_ = {};
        ImGui::TextUnformatted("Loading keyboard image...");
        return;
    }
#endif
    const float width = height * skin.width / skin.height;
    const float scale = std::min(1.f, avail.x / width);
    const ImVec2 size(width * scale, height * scale);
    const float indent = std::max(0.f, (avail.x - size.x) * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    const auto pos = ImGui::GetCursorScreenPos();
    skin_screen_pos_ = {pos.x, pos.y};
    skin_screen_size_ = {size.x, size.y};
    ImGui::Image((ImTextureID)(intptr_t)skin_texture_, size);
    update_lcd_texture();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 a(pos.x + skin.lcd_x / skin.width * size.x, pos.y + skin.lcd_y / skin.height * size.y), b(
                     pos.x + (skin.lcd_x + skin.lcd_w) / skin.width * size.x, pos.y + (skin.lcd_y + skin.lcd_h) / skin.height * size.y);
    draw->AddImage((ImTextureID)(intptr_t)lcd_texture_, a, b);
    const auto now = SDL_GetTicksNS();
    for (const auto& r : skin.keys) {
        const auto index = static_cast<std::size_t>(r.key);
        const bool active = emulator_.keyboard().pressed(r.key) || key_flash_until_ns_[index] > now;
        if (!active && !latched_keys_[index])
            continue;
        const ImVec2 ka(pos.x + r.x / skin.width * size.x, pos.y + r.y / skin.height * size.y), kb(
                         pos.x + (r.x + r.w) / skin.width * size.x, pos.y + (r.y + r.h) / skin.height * size.y);
        const float rounding = std::max(2.f, 8.f * size.x / skin.width);
        if (active)
            draw->AddRectFilled(ka, kb,IM_COL32(42, 214, 194, 68), rounding);
        draw->AddRect(ka, kb, latched_keys_[index] ? IM_COL32(255, 183, 77, 245) : IM_COL32(74, 226, 207, 235), rounding, 0,
                      latched_keys_[index] ? 3.f : 2.f);
        if (latched_keys_[index])
            draw->AddCircleFilled(ImVec2(kb.x - 5.f, kb.y - 5.f), 3.f,IM_COL32(255, 183, 77, 255));
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
        set_pointer_key(-1, ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, true);
    if (ImGui::IsMouseReleased(0))
        set_pointer_key(-1, 0, 0, false);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
        if (const auto key = key_at(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y))
            toggle_latched_key(*key);
}

void FrontendApp::update_lcd_texture() {
    std::array<std::uint32_t, el9950::kLcdWidth * el9950::kLcdHeight> pixels{};
    const auto& fb = emulator_.framebuffer();
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = fb[i] ? 0x17201bffU : 0xb6c5b8ffU;
    SDL_UpdateTexture(lcd_texture_, nullptr, pixels.data(), el9950::kLcdWidth * sizeof(std::uint32_t));
}

void FrontendApp::render_debugger() {
    if (ImGui::BeginTabBar("debug-tabs")) {
        if (ImGui::BeginTabItem("CPU")) {
            const auto s = emulator_.cpu_state();
            ImGui::Text("PC  %s", hex24(s.pc).c_str());
            ImGui::Text("SR  %08X", s.sr);
            ImGui::Text("XSP %08X", s.xssp);
            ImGui::SeparatorText("Register banks");
            for (int b = 0; b < 4; ++b)
                ImGui::Text("%d  XWA %08X  XBC %08X  XDE %08X  XHL %08X", b, s.xwa_xbc_xde_xhl[b * 4], s.xwa_xbc_xde_xhl[b * 4 + 1],
                            s.xwa_xbc_xde_xhl[b * 4 + 2], s.xwa_xbc_xde_xhl[b * 4 + 3]);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Disassembly")) {
            auto address = emulator_.cpu_state().pc;
            for (int row = 0; row < 24; ++row) {
                const auto line = el9950::disassemble(emulator_.bus(), address);
                if (address == emulator_.cpu_state().pc)
                    ImGui::TextColored(ImVec4(.3f, .85f, .75f, 1), "> %06X  %s", address, line.text.c_str());
                else
                    ImGui::Text("  %06X  %s", address, line.text.c_str());
                address += std::max(1, int(line.length));
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory")) {
            static int address = el9950::kRamBase;
            ImGui::InputInt("Address", &address, 1, 16, ImGuiInputTextFlags_CharsHexadecimal);
            for (int row = 0; row < 16; ++row) {
                ImGui::Text("%06X:", address + row * 16);
                ImGui::SameLine();
                for (int col = 0; col < 16; ++col) {
                    ImGui::Text("%02X", emulator_.bus().peek8(address + row * 16 + col));
                    if (col != 15)
                        ImGui::SameLine();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Keyboard")) {
            for (int i = 0; i < static_cast<int>(el9950::Key::count); ++i) {
                const auto key = static_cast<el9950::Key>(i);
                if (emulator_.keyboard().pressed(key)) {
                    ImGui::Text("%s", el9950::Keyboard::name(key, emulator_.keyboard_side()).data());
                    ImGui::SameLine();
                }
            }
            ImGui::NewLine();
            ImGui::Text("Chord mask: %016llX", static_cast<unsigned long long>(emulator_.keyboard().pressed_mask()));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Breakpoints")) {
            static int bp = el9950::kFlashBase;
            ImGui::InputInt("Address", &bp, 1, 16, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine();
            if (ImGui::Button("Add"))
                emulator_.add_breakpoint(bp);
            std::vector<std::uint32_t> points(emulator_.breakpoints().begin(), emulator_.breakpoints().end());
            std::sort(points.begin(), points.end());
            for (const auto point : points) {
                ImGui::PushID(int(point));
                ImGui::Text("0x%06X", point);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                    emulator_.remove_breakpoint(point);
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("LCD")) {
            ImGui::Text("132 x 64 | page %u | column %u", emulator_.lcd().page(), emulator_.lcd().column());
            ImGui::Text("display %s | contrast %u | invert %s | ADC %s | COM %s", emulator_.lcd().display_on() ? "on" : "off",
                        emulator_.lcd().contrast(), emulator_.lcd().inverted() ? "yes" : "no",
                        emulator_.lcd().adc_reverse() ? "reverse" : "normal", emulator_.lcd().com_reverse() ? "reverse" : "normal");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Link")) {
            for (int line = 0; line < 3; ++line)
                ImGui::Text("P%d  %s", line + 1, emulator_.link().sample(line) ? "released/high" : "driven/low");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

std::filesystem::path FrontendApp::default_state_path() const {
#ifdef __EMSCRIPTEN__
    return std::filesystem::path("/persist") / (emulator_.rom().sha256_hex() + ".el99state");
#else
    char* pref = SDL_GetPrefPath("org.tiplanet", "el9950-emu");
    const std::filesystem::path base = pref ? pref : ".";
    SDL_free(pref);
    return base / (emulator_.rom().sha256_hex() + ".el99state");
#endif
}

void FrontendApp::save_state_file(const std::filesystem::path& path)
try {
    if (path.empty())
        return;
    const auto data = emulator_.save_state();
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    status_ = "State saved to " + path.string();
#ifdef __EMSCRIPTEN__
    EM_ASM({FS.syncfs(false, function(e){if(e)console.error(e)});});
#endif
} catch (const std::exception& e) { status_ = e.what(); }

bool FrontendApp::load_state_file(const std::filesystem::path& path)
try {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot open state");
    const auto n = f.tellg();
    f.seekg(0);
    std::vector<std::uint8_t> d(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(d.data()), n);
    emulator_.load_state(d);
    latched_keys_.fill(false);
    pointer_keys_.clear();
    keyboard_keys_.fill(false);
    key_flash_until_ns_.fill(0);
    if (!update_skin()) return false;
    status_ = "State restored";
    return true;
} catch (const std::exception& e) {
    status_ = e.what();
    return false;
}
