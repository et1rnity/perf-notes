// Usage:
// g++ -std=c++20 -pedantic loader.cpp elf.cpp -o loader
// ./loader

#include "elf.h"

#include <exception>
#include <iostream>

int main() {
  try {
    ELF elf("obj.o");
    elf.ExecuteFunctions();
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
  }
}