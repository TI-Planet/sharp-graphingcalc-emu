#pragma once

// Standalone compatibility surface for the BSD TLCS-900 core. It intentionally
// implements only the MAME primitives used by these source files.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using offs_t = std::uint32_t;

union PAIR {
  std::uint32_t d;
  struct { std::uint16_t l, h; } w;
  struct { std::int16_t l, h; } sw;
  struct { std::uint8_t l, h, h2, h3; } b;
  struct { std::int8_t l, h, h2, h3; } sb;
  constexpr PAIR(std::uint32_t value = 0) : d(value) {}
};

namespace util {
inline const char *format_arg(const std::string &value) { return value.c_str(); }
inline const char *format_arg(const char *value) { return value; }
template <typename T> inline T format_arg(T value) { return value; }
template <typename... Args> std::string string_format(const char *format, Args &&...args) {
  const int size = std::snprintf(nullptr, 0, format, format_arg(std::forward<Args>(args))...);
  if (size <= 0) return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  std::snprintf(result.data(), result.size() + 1, format, format_arg(std::forward<Args>(args))...);
  return result;
}
template <typename... Args> void stream_format(std::ostream &stream, const char *format, Args &&...args) {
  stream << string_format(format, std::forward<Args>(args)...);
}

class disasm_interface {
public:
  class data_buffer {
  public:
    data_buffer(const u8 *data, std::size_t size, offs_t base = 0) : data_(data), size_(size), base_(base) {}
    u8 r8(offs_t address) const {
      if (address < base_ || static_cast<std::size_t>(address - base_) >= size_) return 0xff;
      return data_[address - base_];
    }
  private:
    const u8 *data_; std::size_t size_; offs_t base_;
  };
  static constexpr offs_t LENGTHMASK=0xffff, STEP_OVER=0x10000000, STEP_OUT=0x20000000,
                          STEP_COND=0x40000000, SUPPORTED=0x80000000;
  virtual ~disasm_interface() = default;
  virtual u32 opcode_alignment() const = 0;
  virtual offs_t disassemble(std::ostream &, offs_t, const data_buffer &, const data_buffer &) = 0;
};
} // namespace util
using util::string_format;

inline void logerror(const char *format, ...) {
#ifndef NDEBUG
  va_list args; va_start(args, format); std::vfprintf(stderr, format, args); va_end(args);
#else
  (void)format;
#endif
}

class machine_config {};
using device_type = int;
class device_t { public: virtual ~device_t() = default; };
class address_space_config {};

class address_space {
public:
  using Read = std::function<std::uint8_t(std::uint32_t)>;
  using Write = std::function<void(std::uint32_t, std::uint8_t)>;
  void set_callbacks(Read read, Write write) { read_ = std::move(read); write_ = std::move(write); }
  std::uint8_t read_byte(offs_t address) { return read_ ? read_(address) : 0xff; }
  std::uint16_t read_word_unaligned(offs_t address) {
    return std::uint16_t(read_byte(address)) | (std::uint16_t(read_byte(address + 1)) << 8);
  }
  std::uint32_t read_dword_unaligned(offs_t address) {
    return std::uint32_t(read_word_unaligned(address)) | (std::uint32_t(read_word_unaligned(address + 2)) << 16);
  }
  void write_byte(offs_t address, std::uint8_t value) { if (write_) write_(address, value); }
  void write_word_unaligned(offs_t address, std::uint16_t value) {
    write_byte(address, value & 0xff); write_byte(address + 1, value >> 8);
  }
  void write_dword_unaligned(offs_t address, std::uint32_t value) {
    write_word_unaligned(address, value & 0xffff); write_word_unaligned(address + 2, value >> 16);
  }
private:
  Read read_; Write write_;
};

class device_state_entry {
public:
  explicit device_state_entry(int value = 0) : value_(value) {}
  int index() const { return value_; }
private: int value_;
};

class state_proxy {
public:
  state_proxy &callimport() { return *this; }
  state_proxy &formatstr(const char *) { return *this; }
  state_proxy &noshow() { return *this; }
};

class device_memory_interface {
public:
  using space_config_vector = std::vector<std::pair<int, const address_space_config *>>;
};

class cpu_device : public device_t, public device_memory_interface {
public:
  cpu_device(const machine_config &, device_type, const char *, device_t *, std::uint32_t) {}
  virtual void device_start() {}
  virtual void device_reset() {}
  virtual std::uint32_t execute_min_cycles() const noexcept { return 1; }
  virtual std::uint32_t execute_max_cycles() const noexcept { return 1; }
  virtual bool execute_input_edge_triggered(int) const noexcept { return false; }
  virtual void execute_run() {}
  virtual space_config_vector memory_space_config() const { return {}; }
  virtual void state_import(const device_state_entry &) {}
  virtual void state_string_export(const device_state_entry &, std::string &) const {}
  virtual std::unique_ptr<util::disasm_interface> create_disassembler() { return {}; }
  address_space &space(int) { return program_; }
  void set_program_callbacks(address_space::Read read, address_space::Write write) {
    program_.set_callbacks(std::move(read), std::move(write));
  }
  template <typename T> void save_item(T &&) {}
  template <typename T> state_proxy state_add(int, const char *, T &) { return {}; }
  void set_icountptr(int &) {}
  void debugger_wait_hook() {}
  void debugger_instruction_hook(std::uint32_t) {}
private:
  address_space program_;
};

#define ATTR_COLD
#define NAME(x) x
inline constexpr int AS_PROGRAM = 0;
inline constexpr int INPUT_LINE_NMI = 0;
inline constexpr int STATE_GENPC = -1;
inline constexpr int STATE_GENPCBASE = -2;
inline constexpr int STATE_GENFLAGS = -3;
