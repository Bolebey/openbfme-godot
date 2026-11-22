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
`SparseMatchFinder<>`) is not library. A function with no decomp name is never listed here unless a task disassembles it
and records it in PROVEN_LIBRARY with its reason: otherwise it stays `none` until a task classifies it. A wrong entry
would hide a real gap: add by hand only with the reason, and review the diff.
"""
from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

STL = re.compile(r"(^|::|\b)(std::|_STL::|stlp_std::|_Tree|_Rb_tree|_Hashtable|_Ht_iterator<>|hashtable<>|basic_string|allocator|"
                 r"_Destroy<>|__destroy_aux<>|__uninitialized_copy<>|__uninitialized_fill_n<>|__copy<>|__copy_ptrs<>|_Construct<>|"
                 r"_M_create_node|_M_find<>|"
                 r"(map|multimap|vector|list|set|multiset|hash_map|hash_set|hash_multimap|deque|pair|less|equal_to)<>)")
STRINGS = re.compile(r"(^|::)(AsciiString|UnicodeString|StringBase<>|EAStringC)(::|$)")
CRT = re.compile(r"^_{1,2}(SEH_prolog|SEH_epilog|memset|memcpy|memmove|memcmp|strcmp|strncmp|strcpy|strncpy|strcat|strncat|strlen|stricmp|"
                 r"strnicmp|atexit|atoi|atof|ftol2?|tolower|toupper|isprint|isspace|isalpha|isdigit|isalnum|upper_bound|lower_bound|"
                 r"security_check_cookie|CxxThrowException|purecall|allmul|alldiv|aulldiv|allrem|aullrem|chkstk|alloca_probe)$")
# the Lua 4 virtual machine the game links (its C API `_lua_*` and the internals around it): the port runs its own Lua
LUA_API = re.compile(r"^_lua[A-Za-z_]")
DEBUG = re.compile(r"^_bfme_debug|^DebugLog|^DEBUG_")
COMPILER = re.compile(r"`(scalar|vector) deleting destructor'")

# Functions proven by instruction-level disassembly in Local 135 (issue #852) to be library, allocator, string,
# container, or VM infrastructure that the port replaces wholesale.
PROVEN_LIBRARY: dict[int, tuple[str, str]] = {
    # String class (AsciiString / UnicodeString)
    0x0040511A: ("AsciiString::concat", "string class (AsciiString / UnicodeString)"),
    0x00405324: ("StringBase<char>::compareNoCase", "string class (AsciiString / UnicodeString)"),
    0x00435DE0: ("AsciiString::ensureUniqueBufferOfSize", "string class (AsciiString / UnicodeString)"),
    0x00455176: ("AsciiString::operator<", "string class (AsciiString / UnicodeString)"),
    0x008664B9: ("AsciiString::~AsciiString_memberThunk", "string class (AsciiString / UnicodeString)"),
    # Ref-counted smart pointers, memory pools, and spinlock / thread helpers
    0x00401DEB: ("MemoryPoolSingleBlock::allocateBlock", "port infrastructure or library (memory pool / allocator)"),
    0x0045121F: ("BfmePoolRef10::~BfmePoolRef10", "port infrastructure or library (ref-counted smart pointer)"),
    0x00464F76: ("SpinLock::lock", "port infrastructure or library (synchronization primitive)"),
    0x005D91D1: ("OpaqueRefElement4::operator=", "port infrastructure or library (ref-counted smart pointer)"),
    0x006633EE: ("LockedFreeList::push", "port infrastructure or library (memory pool / allocator)"),
    0x0069C0F1: ("MemoryPool::allocate", "port infrastructure or library (memory pool / allocator)"),
    0x0086393B: ("UpgradeModule::~UpgradeModule_baseThunk", "C runtime / compiler helper"),
    0x00A242B0: ("ThreadClass::Switch_Thread", "port infrastructure or library (synchronization primitive)"),
    # CRT / compiler / D3DX thunks
    0x00401399: ("__ehvec_ctor", "C runtime / compiler helper"),
    0x00A3CE22: ("__ArrayUnwind", "C runtime / compiler helper"),
    0x00A3CE80: ("__ehvec_dtor", "C runtime / compiler helper"),
    0x00A3CEC8: ("__ehvec_dtor_unwind", "C runtime / compiler helper"),
    0x00A3D282: ("_memmove", "C runtime / compiler helper"),
    0x00A3D79A: ("__strcmpi", "C runtime / compiler helper"),
    0x00A3ED38: ("D3DXVec3CatmullRom", "C runtime / compiler helper"),
    # STLport containers, iterators, and algorithms
    0x00423450: ("_STL::_Rb_global::_M_minimum", "C++ standard library container (STLport)"),
    0x00423470: ("_STL::_Rb_global::_M_maximum", "C++ standard library container (STLport)"),
    0x0042C320: ("_STL::make_pair<AsciiString,AsciiString>", "C++ standard library container (STLport)"),
    0x004BC9CF: ("_STL::_Destroy", "C++ standard library container (STLport)"),
    0x005DCEC8: ("_STL::_Rb_tree::_M_create_node", "C++ standard library container (STLport)"),
    0x005E0A39: ("_STL::hashtable::begin", "C++ standard library container (STLport)"),
    0x005EB0E5: ("_STL::list::_M_create_node", "C++ standard library container (STLport)"),
    0x005F19CD: ("_STL::vector::_M_fill_insert", "C++ standard library container (STLport)"),
    0x005FFE47: ("_STL::list::~list", "C++ standard library container (STLport)"),
    0x0060453C: ("_STL::pair::pair", "C++ standard library container (STLport)"),
    0x006051B0: ("_STL::make_pair", "C++ standard library container (STLport)"),
    0x006051CB: ("_STL::make_pair", "C++ standard library container (STLport)"),
    0x00606604: ("_STL::operator<<pair<AsciiString,AsciiString>>", "C++ standard library container (STLport)"),
    0x00606645: ("_STL::_Rb_tree::_M_create_node", "C++ standard library container (STLport)"),
    0x00606667: ("_STL::_Rb_tree::_M_create_node", "C++ standard library container (STLport)"),
    0x00610D25: ("_STL::find<const int*,int>", "C++ standard library container (STLport)"),
    0x006191BC: ("_STL::allocator::allocate", "C++ standard library container (STLport)"),
    0x0068CEBB: ("AttributeModifierPoolEntry::operator=", "C++ standard library container (STLport)"),
    0x0068FF46: ("_STL::__copy", "C++ standard library container (STLport)"),
    0x00691D82: ("_STL::__copy_ptrs", "C++ standard library container (STLport)"),
    0x00692F84: ("_STL::_Rb_tree::_M_insert", "C++ standard library container (STLport)"),
    0x00697BF0: ("_STL::_Destroy", "C++ standard library container (STLport)"),
    0x0069C0D4: ("_STL::__copy_ptrs", "C++ standard library container (STLport)"),
    0x006A88CF: ("_STL::pair<AsciiString&,AsciiString&>::operator=", "C++ standard library container (STLport)"),
    0x006ADB12: ("_STL::equal<AsciiString*>", "C++ standard library container (STLport)"),
    0x006AE86E: ("_STL::operator==<vector<AsciiString>>", "C++ standard library container (STLport)"),
    0x006D01C1: ("_STL::_Vector_base::_Vector_base", "C++ standard library container (STLport)"),
    0x006D04E6: ("_STL::_Rb_tree::_M_create_node", "C++ standard library container (STLport)"),
    0x006DA381: ("_STL::_Rb_tree::insert_unique", "C++ standard library container (STLport)"),
    0x0073B36C: ("_STL::vector<int>::_M_fill_insert", "C++ standard library container (STLport)"),
    0x0073C8B4: ("_STL::_Rb_tree::find", "C++ standard library container (STLport)"),
    0x0073DA77: ("_STL::_Rb_tree::_M_insert", "C++ standard library container (STLport)"),
    0x007628CF: ("_STL::allocator<ObjectFilter>::allocate", "C++ standard library container (STLport)"),
    0x007628FD: ("_STL::operator==<vector<int>>", "C++ standard library container (STLport)"),
    0x00762C65: ("ObjectFilter::~ObjectFilter", "C++ standard library container (STLport)"),
    0x00762CE5: ("ObjectFilter::ObjectFilter", "C++ standard library container (STLport)"),
    0x007630FA: ("_STL::_Construct<ObjectFilter>", "C++ standard library container (STLport)"),
    0x00763127: ("_STL::__uninitialized_copy<ObjectFilter*>", "C++ standard library container (STLport)"),
    0x00763156: ("_STL::__uninitialized_fill_n<ObjectFilter*>", "C++ standard library container (STLport)"),
    0x0076323B: ("_STL::__destroy_aux<ObjectFilter*>", "C++ standard library container (STLport)"),
    0x00763257: ("_STL::_Destroy<ObjectFilter*>", "C++ standard library container (STLport)"),
    0x007632E1: ("_STL::vector<ObjectFilter>::~vector", "C++ standard library container (STLport)"),
    0x007633E0: ("_STL::vector<ObjectFilter>::push_back", "C++ standard library container (STLport)"),
    0x00777E32: ("_STL::_List_node::allocate", "C++ standard library container (STLport)"),
    0x00794353: ("_STL::_List_node<AsciiString>::allocate", "C++ standard library container (STLport)"),
    0x0079FEAF: ("_STL::less<pair<NameKeyType,AsciiString>>", "C++ standard library container (STLport)"),
    0x007A14A0: ("_STL::_Rb_tree::find", "C++ standard library container (STLport)"),
    0x0080508C: ("AttributeModifierPoolEntry::AttributeModifierPoolEntry", "C++ standard library container (STLport)"),
    0x008051C5: ("AttributeModifierPoolEntry::AttributeModifierPoolEntry_copy", "C++ standard library container (STLport)"),
    0x008051F0: ("_STL::_Construct<AttributeModifierPoolEntry>", "C++ standard library container (STLport)"),
    0x0080521D: ("_STL::__uninitialized_copy<AttributeModifierPoolEntry*>", "C++ standard library container (STLport)"),
    0x00805243: ("_STL::__uninitialized_fill_n<AttributeModifierPoolEntry*>", "C++ standard library container (STLport)"),
    0x0081368A: ("_STL::hashtable::_M_bkt_num_key", "C++ standard library container (STLport)"),
    0x00828D5C: ("_STL::_STLP_alloc_proxy::_STLP_alloc_proxy", "C++ standard library container (STLport)"),
    0x0083692A: ("_STL::hashtable::find", "C++ standard library container (STLport)"),
    0x008E5A05: ("_STL::__copy", "C++ standard library container (STLport)"),
    0x008EA106: ("_STL::vector<void*>::_M_fill_insert", "C++ standard library container (STLport)"),
    0x008FAE55: ("_STL::__uninitialized_fill_n<int*>", "C++ standard library container (STLport)"),
    0x00945661: ("_STL::equal<const int*,const int*>", "C++ standard library container (STLport)"),
    0x00A3ABD0: ("_STL::__uninitialized_copy", "C++ standard library container (STLport)"),
    0x00A3AC20: ("_STL::__uninitialized_fill_n", "C++ standard library container (STLport)"),
    0x00A3AC70: ("_STL::__copy", "C++ standard library container (STLport)"),
    0x00A3B930: ("_STL::vector::_M_fill_insert", "C++ standard library container (STLport)"),
    # Lua 4 VM internals (within 0x00B5B180..0x00B64CC0)
    0x00B60E20: ("_luaD_openh", "Lua virtual machine (the port runs its own Lua)"),
    0x00B60F20: ("_callhook", "Lua virtual machine (the port runs its own Lua)"),
    0x00B60F80: ("_luaD_callC", "Lua virtual machine (the port runs its own Lua)"),
    0x00B60FF0: ("_luaD_call", "Lua virtual machine (the port runs its own Lua)"),
    0x00B616E0: ("_luaG_getline", "Lua virtual machine (the port runs its own Lua)"),
    0x00B61B40: ("_luaG_symbexec", "Lua virtual machine (the port runs its own Lua)"),
    0x00B61D00: ("_luaG_findobjname", "Lua virtual machine (the port runs its own Lua)"),
    0x00B62310: ("_luaO_equalObj", "Lua virtual machine (the port runs its own Lua)"),
    0x00B62420: ("_luaO_verror", "Lua virtual machine (the port runs its own Lua)"),
    0x00B62F20: ("_adjust_varargs", "Lua virtual machine (the port runs its own Lua)"),
    0x00B63030: ("_luaV_execute", "Lua virtual machine (the port runs its own Lua)"),
    0x00B64380: ("_newlstr", "Lua virtual machine (the port runs its own Lua)"),
    0x00B64650: ("_luaH_mainposition", "Lua virtual machine (the port runs its own Lua)"),
    0x00B64CC0: ("_luaM_realloc", "Lua virtual machine (the port runs its own Lua)"),
    # Apt EAStringC and pool allocator infrastructure
    0x00AE6A60: ("AptPool::getBlockFlag", "port infrastructure or library (Apt memory pool)"),
    0x00AE6B70: ("AptPool::allocBlock", "port infrastructure or library (Apt memory pool)"),
    0x00AE7040: ("EAStringC::FreeData", "string class (AsciiString / UnicodeString)"),
    0x00AE70C0: ("EAStringC::IsEmpty", "string class (AsciiString / UnicodeString)"),
    0x00AE70D0: ("EAStringC::GetLength", "string class (AsciiString / UnicodeString)"),
    0x00AE70E0: ("EAStringC::GetHash", "string class (AsciiString / UnicodeString)"),
    0x00AE7130: ("EAStringC::EAStringC", "string class (AsciiString / UnicodeString)"),
    0x00AE71B0: ("EAStringC::~EAStringC", "string class (AsciiString / UnicodeString)"),
    0x00AE71D0: ("EAStringC::operator=", "string class (AsciiString / UnicodeString)"),
    0x00AE7230: ("EAStringC::IsEqualTo", "string class (AsciiString / UnicodeString)"),
    0x00AE7610: ("EAStringC::Empty", "string class (AsciiString / UnicodeString)"),
    0x00AE7700: ("EAStringC::CompareNoCase", "string class (AsciiString / UnicodeString)"),
    0x00AE7890: ("EAStringC::IsEqualToNoCase", "string class (AsciiString / UnicodeString)"),
    0x00AE78F0: ("EAStringC::Reserve", "string class (AsciiString / UnicodeString)"),
    0x00AE7990: ("EAStringC::IsNotEmpty", "string class (AsciiString / UnicodeString)"),
    0x00AE79A0: ("EAStringC::ComputeHash", "string class (AsciiString / UnicodeString)"),
    0x00AE7D60: ("EAStringC::SetLength", "string class (AsciiString / UnicodeString)"),
    # Local 130 (issue #829) proven CRT, compiler, STLport, allocator, string, and smart-pointer helpers:
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
    if address in PROVEN_LIBRARY:
        return PROVEN_LIBRARY[address][1]
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
    with a.map.open(encoding="utf-8") as f:
        table = list(csv.DictReader(f, delimiter="\t"))
    bare = lambda r: re.sub(r"\s*\(tier [A-Z]\)\s*$", "", r.get("decomp") or "").strip()
    lua = [int(r["function"], 16) for r in table if LUA_API.match(bare(r))]
    lua_block = (min(lua), max(lua)) if lua else (0, 0)  # the span of the `_lua*` functions: the VM's own code
    for r in table:
        address = int(r["function"], 16)
        why = classify(r.get("decomp", ""), address, lua_block)
        if why and address not in rows:
            label = bare(r) or (PROVEN_LIBRARY[address][0] if address in PROVEN_LIBRARY else f"sub_{address:08X}")
            rows[address] = (label, why)
    for address, (label, why) in PROVEN_LIBRARY.items():
        if address not in rows:
            rows[address] = (label, why)
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
