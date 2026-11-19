// clang-format off
// LLVM 20 workflow, run from the repository root.
// Layout below verified with Clang 20.1.2, Linux x86-64, Itanium C++ ABI.
//
// 1. Build and run; name() prints encoded type names such as P1A and 1D:
// clang++-20 -std=c++20 -O0 -g -Wall -Wextra -pedantic cpp/language/rtti.cpp -o /tmp/rtti
// /tmp/rtti
//
// 2. List RTTI/vtable symbols, then demangle them:
// llvm-nm-20 /tmp/rtti | llvm-cxxfilt-20 | rg 'typeinfo (for|name for) [ABSDV]$|vtable for D$'
// llvm-cxxfilt-20 _ZTI1D _ZTS1D _ZTV1D
// Output: typeinfo for D / typeinfo name for D / vtable for D (one per line).
// Encoded TYPE names from name() need --types, unlike _Z-prefixed symbols:
// llvm-cxxfilt-20 --types P1A 1D
// Output: A* / D (one per line). Do not pipe all prose output through --types.
//
// 3. Dump RTTI contents as assembly with symbolic pointer targets:
// clang++-20 -std=c++20 -O0 -S cpp/language/rtti.cpp -o /tmp/rtti.s
// sed -n '/^_ZTI1D:/,/^[[:space:]]*\.size[[:space:]]*_ZTI1D/p' /tmp/rtti.s
// sed -n '/^_ZTI1V:/,/^[[:space:]]*\.size[[:space:]]*_ZTI1V/p' /tmp/rtti.s
// rg -n -A 12 '^_ZTI1[ABSDV]:|^_ZTS1[ABSDV]:|^_ZTV1D:' /tmp/rtti.s
// llvm-cxxfilt-20 < /tmp/rtti.s > /tmp/rtti.demangled.s
//
// 4. Compare object layout with vtable layout (these do not dump RTTI payloads):
// clang++-20 -std=c++20 -Xclang -fdump-record-layouts -fsyntax-only cpp/language/rtti.cpp > /tmp/rtti.records.txt
// sed -n '/^ *0 | struct D$/,/^$/p' /tmp/rtti.records.txt
// clang++-20 -std=c++20 -Xclang -fdump-vtable-layouts -c cpp/language/rtti.cpp -o /tmp/rtti.o > /tmp/rtti.vtables.txt
// cat /tmp/rtti.vtables.txt
//
// 5. Inspect raw bytes AND relocations in the unlinked object file:
// llvm-objdump-20 --full-contents --reloc --section=.data.rel.ro._ZTI1D --section=.rela.data.rel.ro._ZTI1D /tmp/rtti.o
// Pointer bytes may still be zero here; relocations identify their targets.
//
// Symbol prefixes: _ZTV = vtable, _ZTI = typeinfo object, _ZTS = type name.
// For example, _ZTI1D is "typeinfo for D"; _ZTS1D points to the string "1D".
//
// Clang 20.1.2 x86-64 RTTI for D (56 bytes, excludes the name and vtables):
// _ZTI1D:
//   .quad _ZTVN10__cxxabiv121__vmi_class_type_infoE+16  // RTTI object's own vptr
//   .quad _ZTS1D                                    // encoded type name
//   .long 0                                         // inheritance flags
//   .long 2                                         // direct base count
//   .quad _ZTI1A                                    // first base's RTTI
//   .quad 2                                         // (offset 0 * 256) | public
//   .quad _ZTI1B                                    // second base's RTTI
//   .quad 4098                                      // (offset 16 * 256) | public
//
// Each base record holds a typeinfo pointer and offset_flags:
//   low byte: 0x1 = virtual base, 0x2 = public base
//   signed high bits: offset_flags >> 8
// Non-virtual base: this is its byte offset within the derived object.
// Virtual base: this locates a vbase-offset SLOT relative to the vtable address
// point, not the virtual base subobject itself. V typically encodes -24 as -6141:
//   -24 * 256 + 3 = -6141 (public virtual A).
//
// RTTI implementation classes:
//   A, B: __class_type_info (no bases)
//   S:    __si_class_type_info (single public non-virtual base at offset zero)
//   D, V: __vmi_class_type_info (other inheritance layouts)
// RTTI does not describe data members a/b/d, their offsets, or ordinary methods.
// The RTTI object's vptr belongs to the ABI runtime's type_info implementation;
// it is distinct from the vptrs stored inside the user object D.
// clang-format on

#include <iostream>
#include <typeinfo>

struct A {
  virtual ~A() = default;
  int a = 1;
};

struct B {
  virtual ~B() = default;
  int b = 2;
};

struct S : A {};

struct D : A, B {
  int d = 3;
};

struct V : virtual A {};

int main() {
  D object;
  A* a = &object;
  B* b = &object;

  // On this ABI, name() exposes an encoded name; it is not portable text.
  std::cout << "static pointer type: " << typeid(a).name() << '\n';
  std::cout << "dynamic type via A: " << typeid(*a).name() << '\n';
  std::cout << "dynamic type via B: " << typeid(*b).name() << '\n';
  std::cout << "D address: " << static_cast<void*>(&object) << '\n';
  std::cout << "A address: " << static_cast<void*>(a) << '\n';
  std::cout << "B address: " << static_cast<void*>(b) << '\n';
  std::cout << std::boolalpha;
  std::cout << "cross-cast A* -> B*: " << (dynamic_cast<B*>(a) == b) << '\n';
  std::cout << "downcast B* -> D*: " << (dynamic_cast<D*>(b) == &object)
            << '\n';
  std::cout << "unrelated A* -> S*: " << (dynamic_cast<S*>(a) == nullptr)
            << '\n';

  // Force RTTI for all three class representations to be emitted.
  std::cout << "type names: " << typeid(A).name() << ' ' << typeid(B).name()
            << ' ' << typeid(S).name() << ' ' << typeid(D).name() << ' '
            << typeid(V).name() << '\n';
}
