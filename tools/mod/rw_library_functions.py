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
                 r"(map|multimap|vector|list|set|multiset|hash_map|hash_set|hash_multimap|deque|pair|less|equal_to)<>)")
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

    # Local 136 (fork issue 853): 127 shared helpers (56 EA Apt VM + 71 STLport/CRT/MemoryPool/AsciiString/smart pointers/debug).
    0x004050E6: ('AsciiString::set(const char*)', 'string class (AsciiString / UnicodeString)'),
    0x00424DA0: ('STLport::vector<void*>::vector(size_t)', 'C++ standard library container (STLport)'),
    0x0042C280: ('AsciiStringRef::write', 'string class (AsciiString / UnicodeString)'),
    0x00435F80: ('AsciiString::ensureUniqueBufferOfSize', 'string class (AsciiString / UnicodeString)'),
    0x00436370: ('AsciiString::concat', 'string class (AsciiString / UnicodeString)'),
    0x0044C43F: ('RvaSmartPtr12::attach', 'port infrastructure or library (intrusive smart-pointer list attach)'),
    0x0044C4BE: ('RvaSmartPtr12::RvaSmartPtr12', 'port infrastructure or library (intrusive smart-pointer copy ctor)'),
    0x00450315: ('STLport::_Destroy<GeometryShape*>', 'C++ standard library container (STLport)'),
    0x0045032E: ('STLport::vector<GeometryShape>::~vector', 'C++ standard library container (STLport)'),
    0x0045036D: ('STLport::vector<GeometryContactPoint>::~vector', 'C++ standard library container (STLport)'),
    0x00462DBA: ('GeometryShape::GeometryShape(copy)', 'compiler-generated trivial struct copy constructor'),
    0x00463394: ('MixFileCreator::FileInfoStruct::FileInfoStruct(copy)', 'compiler-generated trivial struct copy constructor'),
    0x004633E3: ('MixFileCreator::FileInfoStruct::operator=', 'compiler-generated trivial struct copy assignment'),
    0x00463468: ('STLport::_Construct<FileInfoStruct>', 'C++ standard library container (STLport)'),
    0x00463495: ('STLport::_Construct<BfmeStringRecord00063BE4>', 'C++ standard library container (STLport)'),
    0x00463A62: ('STLport::__uninitialized_copy<BfmeStringRecord00063BE4*>', 'C++ standard library container (STLport)'),
    0x00463A88: ('STLport::__uninitialized_copy<FileInfoStruct*>', 'C++ standard library container (STLport)'),
    0x0047D8B0: ('RefCounted::release', 'port infrastructure or library (ref-counted object release)'),
    0x004AD0EF: ('STLport::allocator<T(0x1C)>::allocate', 'C++ standard library container (STLport)'),
    0x004AD164: ('STLport::_Construct<BfmeStringRecord00111ACF>', 'C++ standard library container (STLport)'),
    0x004AD191: ('STLport::__uninitialized_copy<BfmeStringRecord00111ACF*>', 'C++ standard library container (STLport)'),
    0x004AF99C: ('STLport::__destroy_aux<Rva00111B25Record*>', 'C++ standard library container (STLport)'),
    0x004AFCE2: ('STLport::vector<Rva00111B25Record>::~vector', 'C++ standard library container (STLport)'),
    0x004B386C: ('AsciiStringRef::AsciiStringRef(const char*)', 'string class (AsciiString / UnicodeString)'),
    0x004B3D3B: ('AsciiStringRef::write(memcpy)', 'string class (AsciiString / UnicodeString)'),
    0x004B421B: ('AsciiStringPlusText::operator+', 'string class (AsciiString / UnicodeString)'),
    0x004BB514: ('AsciiStringPlusText::write', 'string class (AsciiString / UnicodeString)'),
    0x004BBCE9: ('AsciiStringPlusText::operator AsciiString', 'string class (AsciiString / UnicodeString)'),
    0x005017E1: ('BfmeStringRecord00111ACF::operator=', 'string class (AsciiString / UnicodeString)'),
    0x0050181D: ('STLport::__copy<BfmeStringRecord00111ACF*>', 'C++ standard library container (STLport)'),
    0x0050187A: ('STLport::vector::_M_allocate_and_copy<BfmeStringRecord00111ACF*>', 'C++ standard library container (STLport)'),
    0x005018A7: ('STLport::__copy_ptrs<BfmeStringRecord00111ACF*>', 'C++ standard library container (STLport)'),
    0x0051147C: ('BfmeStringRecord00111ACF::BfmeStringRecord00111ACF', 'string class (AsciiString / UnicodeString)'),
    0x005114D2: ('Rva00111B25Record::~Rva00111B25Record', 'string class (AsciiString / UnicodeString)'),
    0x005DCFE4: ('STLport::_Destroy_node<AsciiString>', 'C++ standard library container (STLport)'),
    0x005FFE1E: ('STLport::list::list()', 'C++ standard library container (STLport)'),
    0x006172F6: ('GeometryContactPoint::~GeometryContactPoint', 'string class (AsciiString / UnicodeString)'),
    0x00617839: ('STLport::_Destroy<GeometryContactPoint*>', 'C++ standard library container (STLport)'),
    0x0061924B: ('STLport::allocator<T(0x24)>::allocate', 'C++ standard library container (STLport)'),
    0x00655AA8: ('GeometryShape::~GeometryShape', 'string class (AsciiString / UnicodeString)'),
    0x006652A8: ('FreePooledNode', 'port infrastructure or library (MemoryPool::freeBlock)'),
    0x006652BE: ('STLport::__uninitialized_copy<BfmeStringRecord0022074B*>', 'C++ standard library container (STLport)'),
    0x00686160: ('MemoryPool::allocateBlock(0x10)', 'port infrastructure or library (MemoryPool allocator)'),
    0x0068683B: ('Rva00286136Free', 'port infrastructure or library (MemoryPool::freeBlock)'),
    0x006868A8: ('STLport::_Rb_tree::_M_erase', 'C++ standard library container (STLport)'),
    0x00686F85: ('STLport::_Rb_tree::~_Rb_tree', 'C++ standard library container (STLport)'),
    0x00691435: ('empty_ret4_stub', 'compiler-generated empty virtual/callback stub (ret 4)'),
    0x006BF945: ('STLport::allocator<T(0x0C)>::allocate', 'C++ standard library container (STLport)'),
    0x006CF86E: ('_fprintf_appendLine', 'debug hook of the game'),
    0x006DD0AB: ('AsciiStringPlusText::length', 'string class (AsciiString / UnicodeString)'),
    0x007194D6: ('STLport::__copy_ptrs_dword_loop', 'C++ standard library container (STLport)'),
    0x00734B9A: ('STLport::__copy_ptrs<Rva00335C62Element*>', 'C++ standard library container (STLport)'),
    0x0075A646: ('Rva00358B65::~Rva00358B65', 'port infrastructure or library (ref-counted string-pair dtor)'),
    0x0075B554: ('STLport::iterator_copy_ctor_4B', 'C++ standard library container (STLport)'),
    0x0076293A: ('ObjectFilter::poolRelease', 'port infrastructure or library (ObjectFilter intern-pool handle release)'),
    0x007629B0: ('ObjectFilter::~ObjectFilter', 'port infrastructure or library (ObjectFilter intern-pool handle dtor)'),
    0x0076EC18: ('STLport::find<list<UnicodeString>::iterator>', 'C++ standard library container (STLport)'),
    0x007EE233: ('STLport::_List_base::_M_create_node(0x0C)', 'C++ standard library container (STLport)'),
    0x00831762: ('STLport::_Construct<pair>', 'C++ standard library container (STLport)'),
    0x00865C2C: ('STLport::_Rb_tree::_M_erase', 'C++ standard library container (STLport)'),
    0x00865C59: ('STLport::_Rb_tree::_M_erase', 'C++ standard library container (STLport)'),
    0x0086677B: ('STLport::_Rb_tree::~_Rb_tree', 'C++ standard library container (STLport)'),
    0x008667B3: ('STLport::_Rb_tree::~_Rb_tree', 'C++ standard library container (STLport)'),
    0x00866E73: ('STLport::_Rb_tree::_M_erase', 'C++ standard library container (STLport)'),
    0x00867305: ('STLport::_Rb_tree::clear', 'C++ standard library container (STLport)'),
    0x008675B0: ('STLport::_Rb_tree::~_Rb_tree', 'C++ standard library container (STLport)'),
    0x008C18AC: ('InterfaceBase::~InterfaceBase', 'compiler-generated empty base-interface destructor'),
    0x008E3EE9: ('STLport::vector::deallocate', 'C++ standard library container (STLport)'),
    0x00A110BB: ('RefPtr::reset', 'port infrastructure or library (ref-counted smart-pointer reset)'),
    0x00A3D28E: ('__ehvec_ctor', 'C runtime / compiler helper'),
    0x00A3D2D8: ('__ehvec_dtor', 'C runtime / compiler helper'),
    0x00AE7EB0: ('AptVM::sub_00AE7EB0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AE8C40: ('EAStringC::ChangeBuffer', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AE8D90: ('AptVM::sub_00AE8D90', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AE8E20: ('EAStringC::EAStringC', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEA3D0: ('BfmeStrVKK::bfmeTruncVKK', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEAE60: ('Rva006D6D20::Rva006D6D20', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEB3C0: ('AptString::Create', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEF190: ('Rva006DB160::rva006DAFE0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEF240: ('Rva006DB090::rva006DB090', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEF310: ('AptVM::sub_00AEF310', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEF420: ('Rva006DB270::freeBlock', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEF5C0: ('bfmeSkipVF', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEFBD0: ('Rva00897FD0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEFCF0: ('AptValue::getRefCount', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEFD00: ('Rva006DBB30SarDwordField::get', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEFD90: ('BfmeAptValue006DCD20::setTypeAt006DBBC0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEFE40: ('BfmeAptValue006DCD20::setGCRootCount', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEFF80: ('Rva006DBDB0DwordOrSetter::apply', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEFF90: ('Rva006DBDC0DwordMask::clear', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AEFFA0: ('Rva006DBDD0ShrNAndField::get', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF01E0: ('AptVM::sub_00AF01E0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF0250: ('AptVM::sub_00AF0250', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF0390: ('AptVM::sub_00AF0390', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF0430: ('AptVM::sub_00AF0430', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF0480: ('AptVM::sub_00AF0480', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF0750: ('AptVM::sub_00AF0750', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF0E90: ('BfmeAptValue006DCD20::BfmeAptValue006DCD20', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF0EF0: ('BfmeAptValue006DCD20::BfmeAptValue006DCD20', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF10B0: ('BfmeAptValue006DCD20::rva006DCEE0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF10F0: ('BfmeAptValue006DCD20::rva006DCF20', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF1130: ('BfmeAptValue006DCD20::rva006DCF60', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF1890: ('AptVM::sub_00AF1890', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF2A40: ('AptValue::rva006DE870', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF3B70: ('AptVM::sub_00AF3B70', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF44A0: ('AptVM::sub_00AF44A0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF4590: ('AptVM::sub_00AF4590', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF7DA0: ('AptVM::sub_00AF7DA0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AF7DE0: ('AptVM::sub_00AF7DE0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00AFAF10: ('AptValueVector::rva006E6C00', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B0ADB0: ('AptVM::sub_00B0ADB0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B0AF60: ('AptVM::sub_00B0AF60', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B0B1B0: ('BfmeQuery1279::rva006F6FB0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B0FDD0: ('Rva006FBBE0::find', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B10070: ('Rva006FBE80::find2', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B112F0: ('AptVM::sub_00B112F0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B12270: ('AptVM::sub_00B12270', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B128A0: ('AptVM::sub_00B128A0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B12DB0: ('rva006FEC00', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B13F30: ('AptVM::sub_00B13F30', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B1A810: ('AptVM::sub_00B1A810', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B1D8E0: ('AptVM::sub_00B1D8E0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B1D960: ('AptVM::sub_00B1D960', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B1F140: ('AptNativeHash::HashFindKey', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B1F530: ('Rva0070B380::lookup', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B1F6A0: ('Rva0070B4F0GetString', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
    0x00B6CEE0: ('AptVM::sub_00B6CEE0', 'EA Apt Flash/ActionScript VM (the port uses native Godot UI)'),
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
