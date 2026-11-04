#pragma once

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>
#include <type_traits>

class ELF {
 public:
  explicit ELF(const std::filesystem::path& file_path);
  void ExecuteFunctions();

 private:
  struct MappedRegion {
    int fd{-1};
    void* base{MAP_FAILED};
    size_t len{0};
    MappedRegion() = default;
    ~MappedRegion() {
      if (fd >= 0) {
        ::close(fd);
      }
      if (base != MAP_FAILED) {
        ::munmap(base, len);
      }
    }
    MappedRegion(const MappedRegion&) = delete;
    MappedRegion& operator=(const MappedRegion&) = delete;
    MappedRegion(MappedRegion&&) = default;
    MappedRegion& operator=(MappedRegion&&) = default;

    const Elf64_Ehdr* header() const noexcept {
      return static_cast<Elf64_Ehdr*>(base);
    }

    const std::byte* bytes() const noexcept {
      return static_cast<std::byte*>(base);
    }
  };

  [[nodiscard]] const Elf64_Shdr* LookupSection(std::string_view name) noexcept;
  template <typename F>
  F* LookupFunction(std::string_view name);
  void TextRelocation();
  inline std::size_t PageAlign(int n) const noexcept {
    return (n + (page_size_ - 1)) & ~(page_size_ - 1);
  }

  inline std::byte* SectionRuntimeBase(const Elf64_Shdr& section) noexcept {
    auto section_name = shstrtab_ + section.sh_name;
    if (std::string_view(section_name) == kSectionText) {
      return text_runtime_base_;
    }
    if (std::string_view(section_name) == kSectionData) {
      return data_runtime_base_;
    }
    if (std::string_view(section_name) == kSectionReadonlyData) {
      return rodata_runtime_base_;
    }
    return nullptr;
  }

  static constexpr auto kSectionSymbolTable = ".symtab";
  static constexpr auto kSectionStringTable = ".strtab";
  static constexpr auto kSectionText = ".text";
  static constexpr auto kSectionRelaText = ".rela.text";
  static constexpr auto kSectionData = ".data";
  static constexpr auto kSectionReadonlyData = ".rodata";

  // sections table
  std::span<const Elf64_Shdr> sections_;
  // symbols table
  std::span<const Elf64_Sym> symbols_;
  const char* shstrtab_{nullptr};
  const char* strtab_{nullptr};

  const std::size_t page_size_;

  std::byte* text_runtime_base_{nullptr};
  std::byte* data_runtime_base_{nullptr};
  std::byte* rodata_runtime_base_{nullptr};
  MappedRegion mapped_object_;
  MappedRegion runtime_object_;
};