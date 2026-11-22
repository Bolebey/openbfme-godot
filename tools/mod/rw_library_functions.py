#!/usr/bin/env python3
"""The RotWK functions the port never ports one by one: library, compiler and debug code (owner 2026-11-21).

The function map (tools/mod/rw_function_map.py, docs/ennorath/RW_FUNCTION_MAP.tsv) walks every module's call graph; a function
counts as ported only when engine/src cites its first instruction. The walk also reaches code the port replaces wholesale:
the C++ standard library the game linked (STLport containers, strings), the C runtime, compiler-generated helpers (deleting
destructors, SEH prologs), the game's debug hooks, and the engine infrastructure the port has its own of (the INI reader,
Xfer / CRC, the allocator: rw_function_map.HAND_STOPS). Such a function stays uncited forever; counting it as open work
overstates what is left. This tool writes their list, tools/mod/rw_library_functions.tsv (addresses and names only, no
game bytes); the map marks them `library` and the counts (progress_snapshot, MATRIX_GAPS, the task dossiers) leave them out.

    python tools/mod/rw_library_functions.py [--map docs/ennorath/RW_FUNCTION_MAP.tsv] [--out tools/mod/rw_library_functions.tsv]

The rule works on the map's `decomp` column (the BFME2 decomp's name of the matched function, Port 240) and is strict on
purpose: a container or string class of the standard library, a C runtime name (no class, leading underscore), a
compiler-generated destructor, the game's debug hooks, and the hand stop list. A game template (e.g.
`SparseMatchFinder<>`) is not library. A function with no decomp name is never listed here: it stays `none` until a
task classifies it. A wrong entry would hide a real gap: add by hand only with the reason, and review the diff.
"""
from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

STL = re.compile(r"(^|::|\b)(std::|_STL::|stlp_std::|_Tree|_Rb_tree|_Hashtable|_Ht_iterator<>|hashtable<>|basic_string|allocator|"
                 r"(map|multimap|vector|list|set|multiset|hash_map|hash_set|hash_multimap|deque|pair|less|equal_to)<>)|"
                 r"::(__copy|__copy_ptrs|__copy_backward|__copy_backward_ptrs|_Destroy|__destroy_aux|fill|uninitialized_fill_n|find|__find|_M_find)<>$|"
                 r"RefCountPtr<>::operator=$")
STRINGS = re.compile(r"(^|::)(AsciiString|UnicodeString|StringBase<>)(::|$)")
CRT = re.compile(r"^_{1,2}(SEH_prolog|SEH_epilog|memset|memcpy|memmove|memcmp|strcmp|strncmp|strcpy|strncpy|strcat|strncat|strlen|stricmp|"
                 r"strnicmp|atexit|atoi|atof|ftol2?|tolower|toupper|isprint|isspace|isalpha|isdigit|isalnum|upper_bound|lower_bound|"
                 r"security_check_cookie|CxxThrowException|purecall|allmul|alldiv|aulldiv|allrem|aullrem|chkstk|alloca_probe)$")
# the Lua 4 virtual machine the game links (its C API `_lua_*` and the internals around it): the port runs its own Lua
LUA_API = re.compile(r"^_lua[A-Za-z_]")
DEBUG = re.compile(r"^_bfme_debug|^DebugLog|^DEBUG_")
COMPILER = re.compile(r"`(scalar|vector) deleting destructor'")



# RotWK 2.01 addresses proven by disassembly (rw.py dis + IAT) to be CRT, compiler, STLport, allocator,
# AsciiString/UnicodeString, synchronization or ref-counted smart-pointer infrastructure (Local 130, issue #829).
PROVEN_LIBRARY: dict[int, tuple[str, str]] = {
    0x0042C17A: ("STLport::_Construct<AsciiString>", "C++ standard library container (STLport)"),
    0x0042C1A7: ("STLport::uninitialized_fill_n<AsciiString*>", "C++ standard library container (STLport)"),
    0x0042C86C: ("STLport::_Destroy<AsciiString*>", "C++ standard library container (STLport)"),
    0x00430130: ("DynamicMemoryAllocator::rawAlloc(tag=3)", "port infrastructure or library (allocator wrapper [0xDC5E44])"),
    0x00430170: ("DynamicMemoryAllocator::rawFree(tag=3)", "port infrastructure or library (allocator wrapper [0xDC5E3C])"),
    0x004355D0: ("AsciiString::staticCriticalSection", "string class (AsciiString / UnicodeString)"),
    0x00435730: ("UnicodeString::staticCriticalSection", "string class (AsciiString / UnicodeString)"),
    0x004360C0: ("AsciiString::set(const char*, int)", "string class (AsciiString / UnicodeString)"),
    0x00436680: ("AsciiString::format_va", "string class (AsciiString / UnicodeString)"),
    0x004367B0: ("UnicodeString::releaseBuffer", "string class (AsciiString / UnicodeString)"),
    0x00440B21: ("SimpleCriticalSection::SimpleCriticalSection", "C runtime / compiler helper (InitializeCriticalSection)"),
    0x0044A2A5: ("BfmeWideResult::~BfmeWideResult", "port infrastructure or library (ref-counted query result handle)"),
    0x00450762: ("RefCounted::release", "port infrastructure or library (InterlockedDecrement smart-pointer release)"),
    0x0045486C: ("STLport::hash<AsciiString>::operator()", "C++ standard library container (STLport)"),
    0x004A86AC: ("Rva0036CA00Str::Rva0036CA00Str", "port infrastructure or library (InterlockedIncrement smart-pointer copy)"),
    0x004BB149: ("STLport::uninitialized_copy<AsciiString*>", "C++ standard library container (STLport)"),
    0x005FCBC4: ("AsciiString::releaseBuffer_thunk", "string class (AsciiString / UnicodeString)"),
    0x00645716: ("Rva002390CB::Rva002390CB", "port infrastructure or library (smart-pointer pair copy ctor)"),
    0x00671FB0: ("RefPtr<AudioEventInfo>::RefPtr", "port infrastructure or library (InterlockedIncrement smart-pointer ctor)"),
    0x00688E9D: ("STLport::allocator<void*>::allocate", "C++ standard library container (STLport)"),
    0x007AEAFD: ("SecondBase005D2111::~SecondBase005D2111", "compiler-generated empty base-interface destructor"),
    0x00917B0D: ("BfmeWideResultStorage::~BfmeWideResultStorage", "port infrastructure or library (ref-counted query result buffer free)"),
    0x009A2783: ("STLport::_M_create_node(0x18)", "C++ standard library container (STLport)"),
    0x00A340B0: ("STLport::__copy_trivial", "C++ standard library container (STLport)"),
    0x00A3A190: ("BfmeWideResult::BfmeWideResult", "port infrastructure or library (ref-counted query result ctor)"),
    0x00A3CD70: ("__chkstk", "C runtime / compiler helper"),
    0x00A3CF10: ("_strlen_thunk", "C runtime / compiler helper"),
    0x00A3CF22: ("_memcpy_thunk", "C runtime / compiler helper"),
    0x00A3CF4C: ("_onexit", "C runtime / compiler helper"),
    0x00A3D936: ("__dllonexit_thunk", "C runtime / compiler helper"),
}


def classify(name: str, address: int = 0, lua_block: tuple[int, int] = (0, 0)) -> str | None:
    n = re.sub(r"\s*\(tier [A-Z]\)\s*$", "", name or "").strip()
    if not n:
        return None
    if DEBUG.search(n):
        return "debug hook of the game"
    if COMPILER.search(n):
        return "compiler-generated deleting destructor"
    if STL.search(n):
        return "C++ standard library container (STLport)"
    if STRINGS.search(n):
        return "string class (AsciiString / UnicodeString)"
    if "::" not in n and (CRT.match(n) or n.startswith("operator new") or n.startswith("operator delete")):
        return "C runtime / compiler helper"
    if "::" not in n and (LUA_API.match(n) or (0 < lua_block[0] <= address <= lua_block[1] and n.startswith("_") and not n.startswith("_bfme"))):
        return "Lua virtual machine (the port runs its own Lua)"
    return None



# Proven by RotWK 2.01 disassembly (Local 133, client / draw classes): STLport container/string internals,
# WW3D DynamicVectorClass/RefCountPtr/HashTableClass, allocator free, and compiler-generated record helpers
# whose decomp names are unnamed or generic Rva*/Bfme* names.
PROVEN_CLIENT_DRAW_LIBRARY: dict[int, tuple[str, str]] = {
    # W3DHordeModelDraw callees (STLport basic_string, _Rb_tree, list, AsciiString, RefCountClass, record copy/dtor)
    0x00407410: ("std::basic_string<char>::basic_string", "C++ standard library container (STLport)"),
    0x004088C0: ("std::basic_string<char>::_M_range_initialize", "C++ standard library container (STLport)"),
    0x00408D30: ("std::basic_string<char>::basic_string(copy)", "C++ standard library container (STLport)"),
    0x00411D30: ("std::basic_string<char>::erase", "C++ standard library container (STLport)"),
    0x0041B010: ("std::basic_string<char>::append", "C++ standard library container (STLport)"),
    0x004363B0: ("AsciiString::toLower", "string class (AsciiString / UnicodeString)"),
    0x00454778: ("_Rb_tree::_M_create_node", "C++ standard library container (STLport)"),
    0x00478553: ("Rva00752630::Rva00752630(copy)", "compiler-generated struct copy constructor"),
    0x00478A7C: ("Rva000791EDCopy", "C++ standard library container (STLport)"),
    0x00478E00: ("BfmeNarrowRecord00079C23::~BfmeNarrowRecord00079C23", "compiler-generated struct destructor"),
    0x0047900B: ("BfmeNarrowRecord00079C23::BfmeNarrowRecord00079C23", "compiler-generated struct constructor"),
    0x0047946B: ("BfmeNarrowRecord00079C23::BfmeNarrowRecord00079C23(copy)", "compiler-generated struct copy constructor"),
    0x00479A90: ("BfmeNarrowRecord00079C23::operator=", "compiler-generated struct assignment"),
    0x00479CFE: ("BfmeHelperYS::BfmeHelperYS", "compiler-generated struct constructor"),
    0x004B5C5C: ("_Rb_tree::_M_erase", "C++ standard library container (STLport)"),
    0x004B8A90: ("_Rb_tree::clear", "C++ standard library container (STLport)"),
    0x004BB948: ("std::list::erase", "C++ standard library container (STLport)"),
    0x00778D70: ("std::__find(AsciiString)", "C++ standard library container (STLport)"),
    0x00778E1C: ("std::find(AsciiString)", "C++ standard library container (STLport)"),
    0x007985FC: ("_Rb_tree::_M_get_node(header)", "C++ standard library container (STLport)"),
    0x00806CFF: ("std::_Destroy(0x18)", "C++ standard library container (STLport)"),
    0x0082147E: ("std::list::_M_create_node(sentinel)", "C++ standard library container (STLport)"),
    0x00A329C0: ("RefCountClass::Release_Ref", "WW3D ref-counted smart pointer"),
    # W3DSupplyDraw callees (STLport vector/_Rb_tree destructors and compiler-generated record assign/dtor)
    0x004B3C30: ("BfmeRecord000B43E5::operator=", "compiler-generated struct assignment"),
    0x004B604C: ("Rva000B978ARecord::operator=", "compiler-generated struct assignment"),
    0x004B927E: ("Rva000B9AAA::~Rva000B9AAA", "compiler-generated struct destructor"),
    0x004B9296: ("Rva00B9AC2::~Rva00B9AC2", "compiler-generated struct destructor"),
    0x004BAE30: ("_Rb_tree::~_Rb_tree", "C++ standard library container (STLport)"),
    0x004BCA33: ("std::_Destroy(Rva00B9AC2)", "C++ standard library container (STLport)"),
    0x004BD963: ("BfmeVectorRecord000BDF17::~BfmeVectorRecord000BDF17", "compiler-generated struct destructor"),
    0x004BE552: ("Rva000BEDF0Record::~Rva000BEDF0Record", "compiler-generated struct destructor"),
    0x004BFB46: ("std::vector::~vector(AnimSet)", "C++ standard library container (STLport)"),
    0x004BFB85: ("std::vector::~vector(StringTail)", "C++ standard library container (STLport)"),
    0x004BFBC4: ("std::vector::~vector(AsciiString)", "C++ standard library container (STLport)"),
    0x004C046E: ("BfmeStringTailRecord180::~BfmeStringTailRecord180", "compiler-generated struct destructor"),
    0x004C12AC: ("std::vector::~vector(Rva00B9AC2)", "C++ standard library container (STLport)"),
    0x004C12EB: ("std::vector::~vector(Rva000BDD08)", "C++ standard library container (STLport)"),
    0x004C1335: ("std::vector::~vector(Rva00079554Record)", "C++ standard library container (STLport)"),
    0x004C204B: ("BfmePod252::~BfmePod252", "compiler-generated struct destructor"),
    0x004C643E: ("BfmePod248::~BfmePod248", "compiler-generated struct destructor"),
    0x004C827D: ("std::vector::~vector(BfmePod248)", "C++ standard library container (STLport)"),
    0x006CFD22: ("Rva000B435F::operator=", "compiler-generated struct assignment"),
    0x006D049C: ("std::__copy_ptrs(Rva000B435F)", "C++ standard library container (STLport)"),
    # W3DDebrisDraw callees (AsciiString/SplitString/HashTableClass/CRC_Stringi/RefCountPtr)
    0x004356B0: ("skipSeps", "string class (AsciiString / UnicodeString)"),
    0x004356F0: ("skipNonSeps", "string class (AsciiString / UnicodeString)"),
    0x00507E74: ("Ref_Ptr_Set", "WW3D ref-counted smart pointer"),
    0x00508511: ("AsciiStringCharPlusText::make", "string class (AsciiString / UnicodeString)"),
    0x00509687: ("AsciiStringCharPlusText::write", "string class (AsciiString / UnicodeString)"),
    0x0050B479: ("AsciiStringCharPlusText::operator AsciiString", "string class (AsciiString / UnicodeString)"),
    0x0053189B: ("RefCountPtr<TextureClass>::RefCountPtr", "WW3D ref-counted smart pointer"),
    0x00536B39: ("SplitString", "string class (AsciiString / UnicodeString)"),
    0x0091507F: ("AsciiStringCharPlusText::operator+", "string class (AsciiString / UnicodeString)"),
    0x00A279E0: ("HashTableClass::Find", "WW3D container (HashTableClass)"),
    0x00A29500: ("CRC_Stringi", "WW3D hash helper (CRC_Stringi)"),
    # W3DLaserDraw callees (allocator free, DynamicVectorClass, RefCountPtr<TextureClass> vector dtor)
    0x0042F6C0: ("operator delete (pool)", "C runtime / compiler helper"),
    0x0048FA88: ("DynamicVectorClass::Resize", "WW3D container (DynamicVectorClass)"),
    0x004C9105: ("std::_Destroy(RefCountPtr<TextureClass>)", "C++ standard library container (STLport)"),
    0x004C911E: ("std::vector<RefCountPtr<TextureClass>>::~vector", "C++ standard library container (STLport)"),
    0x004FFBF2: ("DynamicVectorClass<Vector3>::Add", "WW3D container (DynamicVectorClass)"),
    0x00525D79: ("RefCountPtr<TextureClass>::~RefCountPtr", "WW3D ref-counted smart pointer"),
    # W3DStreakDraw callees (vector dtor, DynamicVectorClass)
    0x004CFCD4: ("std::vector<AsciiString>::~vector", "C++ standard library container (STLport)"),
    0x004FFB3D: ("DynamicVectorClass::Shrink", "WW3D container (DynamicVectorClass)"),
    0x004FFC33: ("DynamicVectorClass<Vector3>::Delete", "WW3D container (DynamicVectorClass)"),
    0x00567C1D: ("DynamicVectorClass<float>::Add", "WW3D container (DynamicVectorClass)"),
    0x00567C50: ("DynamicVectorClass<float>::Delete", "WW3D container (DynamicVectorClass)"),
    # TerrainResourceClientBehavior callees (_Rb_tree / std::list)
    0x008EC278: ("_Rb_tree::_M_erase", "C++ standard library container (STLport)"),
    0x008EC2EF: ("_Rb_tree::clear", "C++ standard library container (STLport)"),
    0x008EC35C: ("std::list::clear", "C++ standard library container (STLport)"),
    # TransitionDamageFX callees (vector<RubbleNeighbor> dtor)
    0x008BE311: ("RubbleNeighbor::~RubbleNeighbor", "compiler-generated struct destructor"),
    0x008BE48A: ("std::_Destroy(RubbleNeighbor)", "C++ standard library container (STLport)"),
    0x008BE4A3: ("std::vector<RubbleNeighbor>::~vector", "C++ standard library container (STLport)"),
    # W3DQuadrupedDraw callee (std::vector::_M_fill_insert)
    0x004C98D5: ("std::vector<Coord3D>::_M_fill_insert", "C++ standard library container (STLport)"),
    # BeaconClientUpdate callee (RvaSmartPtr12::operator=)
    0x0044C4E2: ("RvaSmartPtr12::operator=", "WW3D ref-counted smart pointer"),
}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--map", type=Path, default=Path("docs/ennorath/RW_FUNCTION_MAP.tsv"))
    ap.add_argument("--out", type=Path, default=Path("tools/mod/rw_library_functions.tsv"))
    a = ap.parse_args(argv)
    from rw_function_map import HAND_STOPS  # here, not at import: load() is used without capstone
    rows = {}
    for address, name in HAND_STOPS.items():
        rows[address] = (name, "port infrastructure or library (rw_function_map.HAND_STOPS)")
    for address, (name, why) in PROVEN_LIBRARY.items():
        if address not in rows:
            rows[address] = (name, why)
    for address, (name, why) in PROVEN_CLIENT_DRAW_LIBRARY.items():
        rows.setdefault(address, (name, why))
    with a.map.open(encoding="utf-8") as f:
        table = list(csv.DictReader(f, delimiter="\t"))
    bare = lambda r: re.sub(r"\s*\(tier [A-Z]\)\s*$", "", r.get("decomp") or "").strip()
    lua = [int(r["function"], 16) for r in table if LUA_API.match(bare(r))]
    lua_block = (min(lua), max(lua)) if lua else (0, 0)  # the span of the `_lua*` functions: the VM's own code
    for r in table:
        address = int(r["function"], 16)
        why = classify(r.get("decomp", ""), address, lua_block)
        if why and address not in rows:
            rows[address] = (bare(r), why)
    with a.out.open("w", encoding="utf-8", newline="") as f:
        f.write("# RotWK functions the port replaces wholesale (tools/mod/rw_library_functions.py): the function map marks them `library`.\n")
        f.write("address\tname\treason\n")
        for address in sorted(rows):
            name, why = rows[address]
            f.write(f"0x{address:08X}\t{name}\t{why}\n")
    print(f"{a.out}: {len(rows)} functions")
    return 0


def load(path: Path = Path(__file__).resolve().parent / "rw_library_functions.tsv") -> dict[int, str]:
    """address -> reason, for the map and the counts; empty when the list is missing."""
    if not path.exists():
        return {}
    out = {}
    with path.open(encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or line.startswith("address\t") or not line.strip():
                continue
            address, _name, why = line.rstrip("\n").split("\t")
            out[int(address, 16)] = why
    return out


if __name__ == "__main__":
    sys.exit(main())
