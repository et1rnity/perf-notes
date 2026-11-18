// clang-format off
// Memory aid: record = layout inside the object; vtable = contents of the table its vptr points to.
// Run from this file's directory; -Xclang passes the next option to the Clang frontend.
// Object layout is determined during semantic analysis, so -fsyntax-only is sufficient.
// clang++-19 -std=c++20 -Xclang -fdump-record-layouts -fsyntax-only virtual_layout.cpp
// <iostream> adds many standard-library layouts. To show only D's complete layout:
// clang++-19 -std=c++20 -Xclang -fdump-record-layouts -fsyntax-only virtual_layout.cpp | sed -n '/^ *0 | struct D$/,/^$/p'
// Replace D with [ABCD] in the pattern to show all four demo classes.
// Vtables are dumped during code generation; -c emits an object file without linking.
// clang++-19 -std=c++20 -Xclang -fdump-vtable-layouts -c virtual_layout.cpp -o /tmp/virtual_layout.o
//
// Record dump: numbers on the left are byte offsets from the complete object's start.
// Indentation shows subobject nesting.
//   sizeof / align   = complete object size (including padding) / required alignment.
//   dsize            = object size excluding tail padding.
//   nvsize / nvalign = size / alignment of the non-virtual portion.
//
// Typical D layout on Linux x86-64 with the Itanium C++ ABI (check the actual dump):
//    0: B subobject's vptr (shared by D and its primary base B)
//    8: B::b
//   12: padding
//   16: C subobject's vptr
//   24: C::c
//   28: D::d (reuses the C subobject's tail padding)
//   32: shared virtual base A, with A::a at offset 32
//   36: padding
//   sizeof=40, dsize=36, align=8, nvsize=32, nvalign=8.
//   Key point: D contains only one A; the most-derived class determines virtual base placement.
//
// Vtable dump: numbers on the left are entry indices, not byte offsets within the object.
// Output from Clang 19.1.7 on Linux x86-64 (Itanium C++ ABI):
//   Vtable for 'D' (6 entries).
//      0 | vbase_offset (32)
//      1 | offset_to_top (0)
//      2 | D RTTI
//          -- (B, 0) vtable address --
//          -- (D, 0) vtable address --
//      3 | vbase_offset (16)
//      4 | offset_to_top (-16)
//      5 | D RTTI
//          -- (C, 16) vtable address --
//
// The six entries form a vtable group: entries 0..2 serve B/D; entries 3..5 serve C.
//   vbase_offset  = byte offset from the current subobject's start to a virtual base.
//   offset_to_top = byte offset from the current subobject's start back to the complete object.
//   RTTI          = type information; once D is constructed, both B/C tables identify D.
// Both paths find the same A: B is at D+0, so 0+32=32; C is at D+16, so 16+16=32.
// To find D's start from C, use offset_to_top: 16+(-16)=0.
//
// "-- (C, 16) vtable address --" means the vptr in the C subobject at D+16 points
// to this address point in the table. The 16 is an OBJECT offset, not a table offset.
// The marker is AFTER RTTI: the vptr points just past RTTI, not to RTTI or the table start.
// With no virtual functions, B/D's address point coincides with entry 3 of the group;
// C's address point is just past entry 5. Metadata is read at NEGATIVE offsets from each:
//   vptr - 24 bytes: vbase_offset (32 for B/D, 16 for C)
//   vptr - 16 bytes: offset_to_top (0 for B/D, -16 for C)
//   vptr -  8 bytes: RTTI (D for both)
//   vptr          : address point (where virtual function entries would normally begin)
//
//   Virtual base offset offsets for 'D' (1 entry).
//      A | -24
// This -24 locates the SLOT holding the distance to A, relative to D's address point.
// It is not the distance to A itself! Conceptually, converting B* to A* does:
//   table = load_vptr(B_address)
//   delta = load_ptrdiff_at(table - 24 bytes)  // reads 32
//   A_address = B_address + delta
// The C* path similarly reads 16 at its own vptr - 24 bytes.
// Memory aid: -24 tells WHERE to read in the table; 32/16 tells HOW FAR to move in the object.
//
//   Even without virtual functions, virtual inheritance needs these entries to locate A.
//   The object stores vptrs; tables are normally shared by objects of the same type
//   and do not contribute to sizeof(D).
//
// Construction vtable: used while constructing a base. RTTI/offset_to_top describe
// the base currently being constructed; virtual base offsets reflect its actual placement
// within the final object. A is typically at +12 in a standalone B, but B -> A is +32 in D.
// The dump also includes:
//   Construction vtable for ('B', 0) in 'D' (3 entries).
//   Construction vtable for ('C', 16) in 'D' (3 entries).
// Each has vbase_offset, offset_to_top, and RTTI, followed by its address point marker.
// Compare the values during base construction and after D is fully constructed:
//   Phase                  vbase_offset   offset_to_top   RTTI
//   B constructor in D          32               0         B
//   C constructor in D          16               0         C
//   D complete, B/D table       32               0         D
//   D complete, C table         16             -16         D
// During C's constructor, "top" is C itself, but A must still be found at its actual
// location inside D. This is why offset_to_top is 0 while vbase_offset remains 16.
// "Original map" is Clang's virtual-method mapping; it is empty here (no virtual functions).
// clang-format on
#include <iostream>
struct A {
  A() { std::cout << "A()\n"; }
  int a;
};
struct B : virtual A {
  B() { std::cout << "B()\n"; }
  int b;
};
struct C : virtual A {
  C() { std::cout << "C()\n"; }
  int c;
};
struct D : B, C {
  // Most-derived D constructs the shared virtual base A; construction order: A -> B -> C -> D.
  D() { std::cout << "D()\n"; }
  int d;
};

int main() {
  // B b;
  D d;
}
