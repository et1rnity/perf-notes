#include "elf.h"

#include <iostream>
#include <utility>

ELF::ELF(const std::filesystem::path& file_path)
    : page_size_(sysconf(_SC_PAGESIZE)) {
  struct stat st;
  int fd = open(file_path.c_str(), O_RDONLY);
  if (fd < 0) {
    throw std::system_error(errno, std::generic_category(), "open ELF file");
  }
  mapped_object_.fd = fd;

  // we need the object size for mmap
  if (::fstat(fd, &st)) {
    throw std::system_error(errno, std::generic_category(), "fstat ELF file");
  }
  mapped_object_.len = st.st_size;

  if (mapped_object_.base = ::mmap(nullptr, mapped_object_.len, PROT_READ,
                                   MAP_PRIVATE, mapped_object_.fd, 0);
      mapped_object_.base == MAP_FAILED) {
    throw std::system_error(errno, std::generic_category(), "mmap ELF file");
  }

  sections_ = {reinterpret_cast<const Elf64_Shdr*>(
                   mapped_object_.bytes() + mapped_object_.header()->e_shoff),
               mapped_object_.header()->e_shnum};
  shstrtab_ = reinterpret_cast<const char*>(
      mapped_object_.bytes() +
      sections_[mapped_object_.header()->e_shstrndx].sh_offset);

  auto symtab_hdr = LookupSection(kSectionSymbolTable);
  if (!symtab_hdr) {
    throw std::runtime_error("Failed to locate section '.symtab'");
  }

  symbols_ = {reinterpret_cast<const Elf64_Sym*>(mapped_object_.bytes() +
                                                 symtab_hdr->sh_offset),
              (symtab_hdr->sh_size / symtab_hdr->sh_entsize)};

  auto strtab_hdr = LookupSection(kSectionStringTable);
  if (!strtab_hdr) {
    throw std::runtime_error("Failed to locate section '.strtab'");
  }

  strtab_ = reinterpret_cast<const char*>(mapped_object_.bytes() +
                                          strtab_hdr->sh_offset);

  // find the `.text` entry in the sections table
  auto text_hdr = LookupSection(kSectionText);
  if (!text_hdr) {
    throw std::runtime_error("Failed to locate section `.strtab`");
  }
  auto data_hdr = LookupSection(kSectionData);
  if (!data_hdr) {
    throw std::runtime_error("Failed to locate section `.data`");
  }
  auto rodata_hdr = LookupSection(kSectionReadonlyData);
  if (!rodata_hdr) {
    throw std::runtime_error("Failed to locate section `.rodata`");
  }

  // allocate memory for `.text`, '.data' and `.rodata` copies rounding up each
  // section to whole pages
  if (runtime_object_.base = static_cast<std::byte*>(
          ::mmap(nullptr,
                 PageAlign(text_hdr->sh_size) + PageAlign(data_hdr->sh_size) +
                     PageAlign(rodata_hdr->sh_size),
                 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
      runtime_object_.base == MAP_FAILED) {
    throw std::runtime_error(
        "Failed to allocate memory for sections [.text, .data, .rodata]");
  }

  text_runtime_base_ = static_cast<std::byte*>(runtime_object_.base);
  data_runtime_base_ = text_runtime_base_ + PageAlign(text_hdr->sh_size);
  rodata_runtime_base_ = data_runtime_base_ + PageAlign(data_hdr->sh_size);

  memcpy(text_runtime_base_, mapped_object_.bytes() + text_hdr->sh_offset,
         text_hdr->sh_size);
  memcpy(data_runtime_base_, mapped_object_.bytes() + data_hdr->sh_offset,
         data_hdr->sh_size);
  memcpy(rodata_runtime_base_, mapped_object_.bytes() + rodata_hdr->sh_offset,
         rodata_hdr->sh_size);

  TextRelocation();

  // make the `.text` copy readonly and executable
  if (mprotect(text_runtime_base_, text_hdr->sh_size, PROT_READ | PROT_EXEC)) {
    throw std::system_error(errno, std::generic_category(),
                            "Failed to make .text executable");
  }

  // make the `.rodata` readonly
  if (mprotect(rodata_runtime_base_, rodata_hdr->sh_size, PROT_READ)) {
    throw std::system_error(errno, std::generic_category(),
                            "Failed to make .rodata readonly");
  }
}

const Elf64_Shdr* ELF::LookupSection(std::string_view name) noexcept {
  for (const auto& section : sections_) {
    if (auto section_name = shstrtab_ + section.sh_name;
        name == std::string_view(section_name) && section.sh_size > 0) {
      return &section;
    }
  }

  return nullptr;
}

void ELF::TextRelocation() {
  auto rela_text_hdr = LookupSection(kSectionRelaText);
  if (!rela_text_hdr) {
    throw std::runtime_error("Failed to locate section .rela.text");
  }

  std::span<const Elf64_Rela> relocations{
      reinterpret_cast<const Elf64_Rela*>(mapped_object_.bytes() +
                                          rela_text_hdr->sh_offset),
      rela_text_hdr->sh_size / rela_text_hdr->sh_entsize};

  for (const auto& rela : relocations) {
    auto symbol_idx = ELF64_R_SYM(rela.r_info);
    auto type = ELF64_R_TYPE(rela.r_info);

    // where to patch .text
    auto patch_offset = text_runtime_base_ + rela.r_offset;
    auto symbol_address =
        SectionRuntimeBase(sections_[symbols_[symbol_idx].st_shndx]) +
        symbols_[symbol_idx].st_value;

    switch (type) {
      case R_X86_64_PC32:
        // S + A - P, 32 bit output, S == L here
      case R_X86_64_PLT32:
        // L + A - P, 32 bit output
        const auto delta = symbol_address + rela.r_addend - patch_offset;
        if (!std::in_range<std::int32_t>(delta)) {
          throw std::runtime_error("Relocation out of range");
        }
        const auto value = static_cast<int32_t>(delta);
        std::memcpy(patch_offset, &value, sizeof(value));
        break;
    }
  }
}

template <typename F>
F* ELF::LookupFunction(std::string_view name) {
  static_assert(std::is_function_v<F>, "F must be a function type");

  auto lookup_func = [this, &name]() -> std::byte* {
    for (const auto& symbol : symbols_) {
      if (auto symbol_name = strtab_ + symbol.st_name;
          std::string_view(symbol_name) == name) {
        if (auto address = SectionRuntimeBase(sections_[symbol.st_shndx]) +
                           symbol.st_value;
            address) {
          return address;
        }
        return nullptr;
      }
    }
    return nullptr;
  };

  auto address = lookup_func();
  if (!address) {
    throw std::runtime_error("Failed to locate function " + std::string(name));
  }

  return reinterpret_cast<F*>(address);
}

void ELF::ExecuteFunctions() {
  const auto add5 = LookupFunction<int(int)>("add5");
  const auto add10 = LookupFunction<int(int)>("add10");
  const auto get_hello = LookupFunction<const char*(void)>("get_hello");
  const auto get_var = LookupFunction<int()>("get_var");
  const auto set_var = LookupFunction<void(int)>("set_var");

  std::cout << "Executing add5...\n";
  std::cout << "add5(42) = " << add5(42) << "\n";

  std::cout << "Executing add10...\n";
  std::cout << "add10(42) = " << add10(42) << "\n";

  std::cout << "Executing get_hello...\n";
  std::cout << "get_hello() = " << get_hello() << "\n";

  std::cout << "Executing get_var...\n";
  std::cout << "get_var() = " << get_var() << "\n";

  std::cout << "Executing set_var...\n";
  set_var(42);

  std::cout << "Executing get_var again...\n";
  std::cout << "get_var() = " << get_var() << "\n";
}