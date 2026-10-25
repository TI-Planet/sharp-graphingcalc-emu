#include "el9950/emulator.hpp"
#include "el9950/debugger.hpp"
#include "el9950/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::vector<std::uint8_t> read_file(const std::string& path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            throw std::runtime_error("cannot open state input: " + path);
        const auto size = input.tellg();
        input.seekg(0);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!input)
            throw std::runtime_error("cannot read state input: " + path);
        return bytes;
    }

    void write_file(const std::string& path, std::span<const std::uint8_t> bytes) {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output)
            throw std::runtime_error("cannot write state output: " + path);
    }

    std::string json_string(std::string_view value) {
        std::ostringstream result;
        result << '"';
        for (const auto character : value) {
            switch (character) {
                case '\\':
                    result << "\\\\";
                    break;
                case '"':
                    result << "\\\"";
                    break;
                case '\n':
                    result << "\\n";
                    break;
                case '\r':
                    result << "\\r";
                    break;
                case '\t':
                    result << "\\t";
                    break;
                default:
                    result << character;
                    break;
            }
        }
        result << '"';
        return result.str();
    }

    std::optional<std::size_t> json_field_marker(std::string_view json, std::string_view key) {
        const auto marker = '"' + std::string(key) + '"';
        std::size_t search_from = 0;
        while (true) {
            const auto position = json.find(marker, search_from);
            if (position == std::string_view::npos)
                return std::nullopt;
            auto after = position + marker.size();
            while (after < json.size() && std::isspace(static_cast<unsigned char>(json[after])))
                ++after;
            if (after < json.size() && json[after] == ':')
                return position;
            search_from = position + marker.size();
        }
    }

    std::optional<std::string> json_string_field(std::string_view json, std::string_view key) {
        const auto marker = '"' + std::string(key) + '"';
        auto found = json_field_marker(json, key);
        if (!found)
            return std::nullopt;
        auto position = *found;
        position = json.find(':', position + marker.size());
        if (position == std::string_view::npos)
            throw std::runtime_error("missing ':' after " + std::string(key));
        while (++position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {}
        if (position >= json.size() || json[position] != '"')
            throw std::runtime_error("field is not a string: " + std::string(key));
        std::string value;
        for (++position; position < json.size(); ++position) {
            const auto character = json[position];
            if (character == '"')
                return value;
            if (character != '\\') {
                value += character;
                continue;
            }
            if (++position >= json.size())
                break;
            switch (json[position]) {
                case '"':
                    value += '"';
                    break;
                case '\\':
                    value += '\\';
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    throw std::runtime_error("unsupported JSON string escape");
            }
        }
        throw std::runtime_error("unterminated JSON string: " + std::string(key));
    }

    std::optional<std::uint64_t> json_integer_field(std::string_view json, std::string_view key) {
        const auto marker = '"' + std::string(key) + '"';
        const auto found = json_field_marker(json, key);
        if (!found)
            return std::nullopt;
        auto position = *found;
        position = json.find(':', position + marker.size());
        if (position == std::string_view::npos)
            throw std::runtime_error("missing ':' after " + std::string(key));
        while (++position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {}
        const auto start = position;
        while (position < json.size() && std::isdigit(static_cast<unsigned char>(json[position])))
            ++position;
        if (position == start)
            throw std::runtime_error("field is not an unsigned integer: " + std::string(key));
        return std::stoull(std::string(json.substr(start, position - start)));
    }

    bool json_boolean_field(std::string_view json, std::string_view key, bool fallback = false) {
        const auto marker = '"' + std::string(key) + '"';
        const auto found = json_field_marker(json, key);
        if (!found)
            return fallback;
        auto position = *found;
        position = json.find(':', position + marker.size());
        if (position == std::string_view::npos)
            throw std::runtime_error("missing ':' after " + std::string(key));
        while (++position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {}
        if (json.substr(position, 4) == "true")
            return true;
        if (json.substr(position, 5) == "false")
            return false;
        throw std::runtime_error("field is not boolean: " + std::string(key));
    }

    bool json_has_field(std::string_view json, std::string_view key) {
        return json_field_marker(json, key).has_value();
    }

    std::vector<std::string> json_string_array_field(std::string_view json, std::string_view key) {
        const auto marker = '"' + std::string(key) + '"';
        const auto found = json_field_marker(json, key);
        if (!found)
            return {};
        auto position = *found;
        position = json.find('[', position + marker.size());
        if (position == std::string_view::npos)
            throw std::runtime_error("field is not an array: " + std::string(key));
        std::vector<std::string> values;
        while (++position < json.size()) {
            while (position < json.size() && (std::isspace(static_cast<unsigned char>(json[position])) || json[position] == ','))
                ++position;
            if (position >= json.size() || json[position] == ']')
                return values;
            if (json[position] != '"')
                throw std::runtime_error("array contains a non-string: " + std::string(key));
            std::string value;
            for (++position; position < json.size() && json[position] != '"'; ++position) {
                if (json[position] == '\\') {
                    if (++position >= json.size())
                        throw std::runtime_error("unterminated array string");
                    if (json[position] != '"' && json[position] != '\\')
                        throw std::runtime_error("unsupported array string escape");
                }
                value += json[position];
            }
            if (position >= json.size())
                throw std::runtime_error("unterminated array string");
            values.push_back(std::move(value));
        }
        throw std::runtime_error("unterminated array: " + std::string(key));
    }

    std::string base64_encode(std::span<const std::uint8_t> bytes) {
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string output;
        output.reserve((bytes.size() + 2) / 3 * 4);
        for (std::size_t index = 0; index < bytes.size(); index += 3) {
            const auto available = std::min<std::size_t>(3, bytes.size() - index);
            std::uint32_t value = std::uint32_t(bytes[index]) << 16;
            if (available > 1)
                value |= std::uint32_t(bytes[index + 1]) << 8;
            if (available > 2)
                value |= bytes[index + 2];
            output += alphabet[(value >> 18) & 63];
            output += alphabet[(value >> 12) & 63];
            output += available > 1 ? alphabet[(value >> 6) & 63] : '=';
            output += available > 2 ? alphabet[value & 63] : '=';
        }
        return output;
    }

    std::vector<std::uint8_t> base64_decode(std::string_view input) {
        auto decode = [](char character) -> int {
            if (character >= 'A' && character <= 'Z')
                return character - 'A';
            if (character >= 'a' && character <= 'z')
                return character - 'a' + 26;
            if (character >= '0' && character <= '9')
                return character - '0' + 52;
            if (character == '+')
                return 62;
            if (character == '/')
                return 63;
            if (character == '=')
                return -2;
            return -1;
        };
        if (input.size() % 4)
            throw std::runtime_error("invalid base64 length");
        std::vector<std::uint8_t> output;
        output.reserve(input.size() / 4 * 3);
        for (std::size_t index = 0; index < input.size(); index += 4) {
            const int a = decode(input[index]), b = decode(input[index + 1]), c = decode(input[index + 2]), d = decode(input[index + 3]);
            if (a < 0 || b < 0 || c == -1 || d == -1)
                throw std::runtime_error("invalid base64 data");
            const auto value = (std::uint32_t(a) << 18) | (std::uint32_t(b) << 12) |
                (std::uint32_t(c < 0 ? 0 : c) << 6) | std::uint32_t(d < 0 ? 0 : d);
            output.push_back((value >> 16) & 0xff);
            if (c != -2)
                output.push_back((value >> 8) & 0xff);
            if (d != -2)
                output.push_back(value & 0xff);
            if ((c == -2 || d == -2) && index + 4 != input.size())
                throw std::runtime_error("invalid base64 padding");
        }
        return output;
    }

    std::string response_prefix(std::string_view request, std::string_view command, bool ok) {
        std::string result = "{\"ok\":" + std::string(ok ? "true" : "false") + ",\"command\":" + json_string(command);
        if (const auto id = json_string_field(request, "id"))
            result += ",\"id\":" + json_string(*id);
        return result;
    }

    std::string status_response(
        const el9950::Emulator& emulator, std::string_view request, std::string_view command,
        std::optional<std::uint64_t> executed = std::nullopt) {
        const auto cpu = emulator.cpu_state();
        const auto& fb = emulator.framebuffer();
        auto result = response_prefix(request, command, true);
        if (executed)
            result += ",\"executed\":" + std::to_string(*executed);
        result += ",\"total_cycles\":" + std::to_string(emulator.cycles()) +
            ",\"pc\":" + std::to_string(cpu.pc) + ",\"sr\":" + std::to_string(cpu.sr) +
            ",\"halted\":" + std::string(cpu.halted ? "true" : "false") +
            ",\"reset_contact_open\":" + std::string(emulator.reset_contact_open() ? "true" : "false") +
            ",\"operating_battery_percent\":" + std::to_string(emulator.operating_battery_percent()) +
            ",\"backup_battery_healthy\":" + std::string(emulator.backup_battery_healthy() ? "true" : "false") +
            ",\"model\":" + json_string(el9950::model_name(emulator.rom().model())) +
            ",\"rom_version\":" + json_string(emulator.rom().version()) +
            ",\"keyboard_side\":" + json_string(emulator.keyboard_side() == el9950::KeyboardSide::basic ? "basic" : "advanced") +
            ",\"pressed_mask\":" + std::to_string(emulator.keyboard().pressed_mask()) +
            ",\"framebuffer_sha256\":" + json_string(el9950::hex_sha256(el9950::sha256(std::span(fb)))) + "}";
        return result;
    }

    void set_keys(el9950::Emulator& emulator, const std::vector<std::string>& ids, bool pressed) {
        for (const auto& id : ids) {
            const auto key = el9950::Keyboard::from_id(id);
            if (!key)
                throw std::runtime_error("unknown key id: " + id);
            emulator.set_key(*key, pressed);
        }
    }

    void control_loop(el9950::Emulator& emulator) {
        std::cout << "{\"event\":\"ready\",\"protocol\":\"el9950-headless\",\"version\":1,\"rom_sha256\":"
            << json_string(emulator.rom().sha256_hex()) << ",\"known_1_20\":"
            << (emulator.known_rom() ? "true" : "false") << "}\n" << std::flush;
        std::string request;
        while (std::getline(std::cin, request)) {
            std::string command;
            try {
                command = json_string_field(request, "command").value_or("");
                if (command.empty())
                    throw std::runtime_error("missing command");
                if (command == "status") {
                    std::cout << status_response(emulator, request, command);
                } else if (command == "run") {
                    const auto limit = json_integer_field(request, "cycles").value_or(0);
                    std::uint64_t executed = 0;
                    if (json_boolean_field(request, "until_halt")) {
                        while (executed < limit && !emulator.cpu_state().halted) {
                            const auto ran = emulator.run_cycles(std::min<std::uint64_t>(4096, limit - executed));
                            executed += ran;
                            if (!ran)
                                break;
                        }
                    } else
                        executed = emulator.run_cycles(limit);
                    std::cout << status_response(emulator, request, command, executed);
                } else if (command == "keys") {
                    set_keys(emulator, json_string_array_field(request, "press"), true);
                    set_keys(emulator, json_string_array_field(request, "release"), false);
                    std::cout << status_response(emulator, request, command);
                } else if (command == "reset") {
                    const auto action = json_string_field(request, "action").value_or("pulse");
                    if (action == "open")
                        emulator.set_reset_contact(true);
                    else if (action == "close")
                        emulator.set_reset_contact(false);
                    else if (action == "pulse") {
                        emulator.set_reset_contact(true);
                        emulator.set_reset_contact(false);
                    } else
                        throw std::runtime_error("reset action must be open, close, or pulse");
                    std::cout << status_response(emulator, request, command);
                } else if (command == "keyboard") {
                    const auto side = json_string_field(request, "side");
                    if (!side || (*side != "advanced" && *side != "basic"))
                        throw std::runtime_error("keyboard requires side advanced or basic");
                    emulator.set_keyboard_side(*side == "basic" ? el9950::KeyboardSide::basic : el9950::KeyboardSide::advanced);
                    std::cout << status_response(emulator, request, command);
                } else if (command == "battery") {
                    if (const auto percent = json_integer_field(request, "operating_percent")) {
                        if (*percent > 100)
                            throw std::runtime_error("operating_percent must be between 0 and 100");
                        emulator.set_operating_battery_percent(static_cast<std::uint8_t>(*percent));
                    }
                    if (json_has_field(request, "backup_healthy"))
                        emulator.set_backup_battery_healthy(json_boolean_field(request, "backup_healthy"));
                    std::cout << status_response(emulator, request, command);
                } else if (command == "irq") {
                    const auto line = json_integer_field(request, "line").value_or(0);
                    if (line >= 16)
                        throw std::runtime_error("IRQ line must be between 0 and 15");
                    const auto action = json_string_field(request, "action").value_or("pulse");
                    if (action == "assert")
                        emulator.set_irq_line(static_cast<std::uint8_t>(line), true);
                    else if (action == "clear")
                        emulator.set_irq_line(static_cast<std::uint8_t>(line), false);
                    else if (action == "pulse")
                        emulator.pulse_irq(static_cast<std::uint8_t>(line));
                    else
                        throw std::runtime_error("IRQ action must be assert, clear, or pulse");
                    std::cout << status_response(emulator, request, command);
                } else if (command == "screenshot") {
                    const auto& fb = emulator.framebuffer();
                    const std::string header = "P5\n132 64\n255\n";
                    std::vector<std::uint8_t> pgm(header.begin(), header.end());
                    pgm.reserve(header.size() + fb.size());
                    for (const auto pixel : fb)
                        pgm.push_back(pixel ? 0x18 : 0xb9);
                    std::cout << response_prefix(request, command, true) << ",\"mime_type\":\"image/x-portable-graymap\""
                        << ",\"width\":132,\"height\":64,\"data_base64\":" << json_string(base64_encode(pgm)) << '}';
                } else if (command == "state.dump") {
                    const auto state = emulator.save_state();
                    std::cout << response_prefix(request, command, true) << ",\"format\":\"el99state\",\"data_base64\":"
                        << json_string(base64_encode(state)) << '}';
                } else if (command == "state.load") {
                    const auto encoded = json_string_field(request, "data_base64");
                    if (!encoded)
                        throw std::runtime_error("missing data_base64");
                    const auto state = base64_decode(*encoded);
                    emulator.load_state(state);
                    std::cout << status_response(emulator, request, command);
                } else if (command == "memory.read") {
                    const auto address = json_integer_field(request, "address").value_or(0);
                    const auto length = json_integer_field(request, "length").value_or(1);
                    if (address > 0xffffff || length > 65536 || length > 0x1000000ULL - address)
                        throw std::runtime_error("memory.read range is too large");
                    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
                    for (std::size_t index = 0; index < data.size(); ++index)
                        data[index] = emulator.bus().peek8(static_cast<std::uint32_t>(address + index));
                    std::cout << response_prefix(request, command, true) << ",\"address\":" << address << ",\"length\":" << length
                        << ",\"data_base64\":" << json_string(base64_encode(data)) << '}';
                } else if (command == "memory.write") {
                    const auto address = json_integer_field(request, "address").value_or(0);
                    const auto value = json_integer_field(request, "value").value_or(0);
                    if (address > 0xffffff || value > 0xff)
                        throw std::runtime_error("memory.write address or value is out of range");
                    emulator.bus().write8(static_cast<std::uint32_t>(address), static_cast<std::uint8_t>(value));
                    std::cout << response_prefix(request, command, true) << ",\"address\":" << address << ",\"value\":" << value << '}';
                } else if (command == "disassemble") {
                    auto address = static_cast<std::uint32_t>(json_integer_field(request, "address").value_or(emulator.cpu_state().pc));
                    const auto count = json_integer_field(request, "count").value_or(16);
                    if (count > 1024)
                        throw std::runtime_error("disassembly count is too large");
                    std::cout << response_prefix(request, command, true) << ",\"lines\":[";
                    for (std::uint64_t index = 0; index < count; ++index) {
                        const auto line = el9950::disassemble(emulator.bus(), address);
                        if (index)
                            std::cout << ',';
                        std::cout << "{\"address\":" << line.address << ",\"length\":" << unsigned(line.length)
                            << ",\"text\":" << json_string(line.text) << '}';
                        address += std::max<std::uint8_t>(line.length, 1);
                    }
                    std::cout << "]}";
                } else if (command == "quit") {
                    std::cout << response_prefix(request, command, true) << '}';
                    std::cout << '\n' << std::flush;
                    return;
                } else
                    throw std::runtime_error("unknown command: " + command);
            } catch (const std::exception& error) {
                std::cout << response_prefix(request, command, false) << ",\"error\":" << json_string(error.what()) << '}';
            }
            std::cout << '\n' << std::flush;
        }
    }
}

int main(int argc, char** argv)
try {
    if (argc < 2) {
        std::cerr << "usage: el9950-headless ROM.bin [cycles] [options]\n"
            "  --json                 emit one machine-readable JSON object\n"
            "  --control              persistent NDJSON control on stdin/stdout\n"
            "  --until-halt MAX       stop when HALT is reached or after MAX cycles\n"
            "  --hold KEY             hold a key by skin id (second, variable, on, ...)\n"
            "  --release KEY          release a key by skin id\n"
            "  --keyboard-side SIDE   select advanced (default) or basic keyboard\n"
            "  --battery PERCENT      set operating batteries from 0 to 100 (default 100)\n"
            "  --backup-depleted      emulate a depleted backup battery\n"
            "  --reset                open and close the battery-compartment contact\n"
            "  --state-in FILE        restore a versioned .el99state before running\n"
            "  --state-out FILE       save a versioned .el99state after running\n"
            "  --trace-io FILE        write an NDJSON device-I/O trace\n"
            "  --disasm ADDRESS COUNT append disassembly (included in JSON mode)\n";
        return 2;
    }
    el9950::Emulator emulator;
    emulator.load_rom(argv[1]);
    std::uint64_t requested = 0;
    bool until_halt = false;
    bool json = false;
    bool control = false;
    std::string state_output;
    std::uint32_t disasm_address = 0;
    unsigned disasm_count = 0;
    std::ofstream trace;
    for (int argument = 2; argument < argc; ++argument) {
        if (std::string_view(argv[argument]) == "--trace-io") {
            if (++argument >= argc)
                throw std::runtime_error("--trace-io requires a path");
            trace.open(argv[argument]);
            if (!trace)
                throw std::runtime_error("cannot open I/O trace");
            emulator.set_io_observer([&trace,&emulator](
                std::uint64_t cycle, bool write,
                std::uint32_t address, std::uint8_t value) {
                    trace << "{\"schema\":1,\"cycle\":" << cycle
                        << ",\"kind\":\"" << (write ? "write8" : "read8")
                        << "\",\"pc\":" << emulator.cpu_state().pc
                        << "\",\"address\":" << address
                        << ",\"value\":" << unsigned(value) << "}\n";
                });
        } else if (std::string_view(argv[argument]) == "--json") {
            json = true;
        } else if (std::string_view(argv[argument]) == "--control") {
            control = true;
        } else if (std::string_view(argv[argument]) == "--until-halt") {
            if (++argument >= argc)
                throw std::runtime_error("--until-halt requires a cycle limit");
            requested = std::strtoull(argv[argument], nullptr, 0);
            until_halt = true;
        } else if (std::string_view(argv[argument]) == "--hold" || std::string_view(argv[argument]) == "--release") {
            const bool pressed = std::string_view(argv[argument]) == "--hold";
            if (++argument >= argc)
                throw std::runtime_error("key option requires a key id");
            const auto key = el9950::Keyboard::from_id(argv[argument]);
            if (!key)
                throw std::runtime_error("unknown key id: " + std::string(argv[argument]));
            emulator.set_key(*key, pressed);
        } else if (std::string_view(argv[argument]) == "--reset") {
            emulator.set_reset_contact(true);
            emulator.set_reset_contact(false);
        } else if (std::string_view(argv[argument]) == "--keyboard-side") {
            if (++argument >= argc) throw std::runtime_error("--keyboard-side requires advanced or basic");
            const std::string_view side = argv[argument];
            if (side != "advanced" && side != "basic") throw std::runtime_error("invalid keyboard side");
            emulator.set_keyboard_side(side == "basic" ? el9950::KeyboardSide::basic : el9950::KeyboardSide::advanced);
        } else if (std::string_view(argv[argument]) == "--battery") {
            if (++argument >= argc)
                throw std::runtime_error("--battery requires a percentage");
            const auto percent = std::strtoul(argv[argument], nullptr, 0);
            if (percent > 100)
                throw std::runtime_error("--battery must be between 0 and 100");
            emulator.set_operating_battery_percent(static_cast<std::uint8_t>(percent));
        } else if (std::string_view(argv[argument]) == "--backup-depleted") {
            emulator.set_backup_battery_healthy(false);
        } else if (std::string_view(argv[argument]) == "--state-in") {
            if (++argument >= argc)
                throw std::runtime_error("--state-in requires a path");
            const auto bytes = read_file(argv[argument]);
            emulator.load_state(bytes);
        } else if (std::string_view(argv[argument]) == "--state-out") {
            if (++argument >= argc)
                throw std::runtime_error("--state-out requires a path");
            state_output = argv[argument];
        } else if (std::string_view(argv[argument]) == "--disasm") {
            if (argument + 2 >= argc)
                throw std::runtime_error("--disasm requires an address and count");
            disasm_address = static_cast<std::uint32_t>(std::strtoul(argv[++argument], nullptr, 0));
            disasm_count = static_cast<unsigned>(std::strtoul(argv[++argument], nullptr, 0));
        } else {
            requested = std::strtoull(argv[argument], nullptr, 0);
        }
    }
    if (control) {
        control_loop(emulator);
        if (!state_output.empty()) {
            const auto state = emulator.save_state();
            write_file(state_output, state);
        }
        return 0;
    }
    std::uint64_t executed = 0;
    if (until_halt) {
        while (executed < requested && !emulator.cpu_state().halted) {
            const auto chunk = std::min<std::uint64_t>(4096, requested - executed);
            const auto ran = emulator.run_cycles(chunk);
            executed += ran;
            if (!ran)
                break;
        }
    } else {
        executed = emulator.run_cycles(requested);
    }
    if (!state_output.empty()) {
        const auto state = emulator.save_state();
        write_file(state_output, state);
    }
    const auto cpu = emulator.cpu_state();
    const auto& fb = emulator.framebuffer();
    const auto framebuffer_hash = el9950::hex_sha256(el9950::sha256(std::span(fb)));
    std::vector<el9950::DisassemblyLine> lines;
    for (unsigned row = 0; row < disasm_count; ++row) {
        const auto line = el9950::disassemble(emulator.bus(), disasm_address);
        lines.push_back(line);
        disasm_address += std::max<std::uint8_t>(line.length, 1);
    }
    if (json) {
        std::cout << "{\"schema\":1,\"rom_sha256\":" << json_string(emulator.rom().sha256_hex())
            << ",\"known_1_20\":" << (emulator.known_rom() ? "true" : "false")
            << ",\"model\":" << json_string(el9950::model_name(emulator.rom().model()))
            << ",\"rom_version\":" << json_string(emulator.rom().version())
            << ",\"keyboard_side\":" << json_string(emulator.keyboard_side() == el9950::KeyboardSide::basic ? "basic" : "advanced")
            << ",\"cycles\":" << executed << ",\"total_cycles\":" << emulator.cycles()
            << ",\"pc\":" << cpu.pc << ",\"sr\":" << cpu.sr
            << ",\"halted\":" << (cpu.halted ? "true" : "false")
            << ",\"operating_battery_percent\":" << unsigned(emulator.operating_battery_percent())
            << ",\"backup_battery_healthy\":" << (emulator.backup_battery_healthy() ? "true" : "false")
            << ",\"framebuffer_sha256\":" << json_string(framebuffer_hash)
            << ",\"disassembly\":[";
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (index)
                std::cout << ',';
            std::cout << "{\"address\":" << lines[index].address << ",\"length\":" << unsigned(lines[index].length)
                << ",\"text\":" << json_string(lines[index].text) << '}';
        }
        std::cout << "]}\n";
    } else {
        std::cout << "rom_sha256=" << emulator.rom().sha256_hex() << '\n'
            << "known_1_20=" << (emulator.known_rom() ? "yes" : "no") << '\n'
            << "model=" << el9950::model_name(emulator.rom().model()) << '\n'
            << "rom_version=" << emulator.rom().version() << '\n'
            << "keyboard_side=" << (emulator.keyboard_side() == el9950::KeyboardSide::basic ? "basic" : "advanced") << '\n'
            << "cycles=" << executed << '\n'
            << "total_cycles=" << emulator.cycles() << '\n'
            << "pc=0x" << std::hex << cpu.pc << '\n'
            << "sr=0x" << cpu.sr << '\n'
            << "halted=" << std::boolalpha << cpu.halted << '\n'
            << "operating_battery_percent=" << std::dec << unsigned(emulator.operating_battery_percent()) << '\n'
            << "backup_battery_healthy=" << std::boolalpha << emulator.backup_battery_healthy() << '\n'
            << "framebuffer_sha256=" << framebuffer_hash << '\n';
        for (const auto& line : lines)
            std::cout << "0x" << std::hex << std::setw(6) << std::setfill('0') << line.address << "  " << line.text << '\n';
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
}
