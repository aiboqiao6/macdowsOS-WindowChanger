#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Read-only view of the exact GF x86 event containers verified in 180a60 /
// 180c60. No framework callback, AddRef, subscription, or event is invoked.
namespace txevents {
struct Binding {
    uint32_t id{};
    std::string name;
    uintptr_t sink{}, table{}, numericCallback{}, namedCallback{};
    bool operator==(const Binding &) const = default;
};
struct Result {
    bool profile{}, complete{};
    unsigned errors{};
    std::vector<Binding> numeric, named, listeners;
};
inline bool Read(uintptr_t address, void *destination, size_t length) {
    SIZE_T read{};
    return address && address + length >= address &&
           ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), destination, length, &read) &&
           read == length;
}
template <class T> inline bool Read(uintptr_t address, T &value) {
    return Read(address, &value, sizeof(value));
}
inline Result Inspect(void *frame, BYTE *image) {
    Result result;
    if (sizeof(void *) != 4 || !frame || !image)
        return result;
    auto object = reinterpret_cast<uintptr_t>(frame), base = reinterpret_cast<uintptr_t>(image);
    uint32_t table{}, container{};
    if (!Read(object, table) || table != base + 0x1f4c00 || !Read(object + 0x12c, container) ||
        container != base + 0x1e9d0c)
        return result;
    result.profile = true;
    auto readSink = [&](uintptr_t sink, Binding &binding) {
        uint32_t methods[9]{};
        uint32_t vtable{};
        if (!Read(sink, vtable) || !Read(vtable, methods, sizeof(methods)))
            return false;
        binding.sink = sink;
        binding.table = vtable;
        binding.numericCallback = methods[7];
        binding.namedCallback = methods[8];
        for (auto method : {methods[7], methods[8]}) {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<void *>(method), &memory, sizeof(memory)) ||
                memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                !(memory.Protect &
                  (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                return false;
        }
        return true;
    };
    auto tree = [&](uintptr_t map, bool named, std::vector<Binding> &bindings) {
        uint32_t head{}, size{}, links[3]{};
        BYTE nil{};
        if (!Read(map, head) || !Read(map + 4, size) || size > 128 || !Read(head, links, sizeof(links)) ||
            !Read(head + 0xd, nil) || nil != 1)
            return false;
        std::set<uintptr_t> seen;
        std::function<bool(uintptr_t, uintptr_t, unsigned)> walk = [&](uintptr_t node, uintptr_t parent,
                                                                       unsigned depth) {
            if (node == head)
                return true;
            uint32_t child[3]{};
            BYTE sentinel{};
            if (!node || depth > 32 || seen.size() >= size || !seen.insert(node).second ||
                !Read(node, child, sizeof(child)) || child[1] != parent || !Read(node + 0xd, sentinel) || sentinel)
                return false;
            if (!walk(child[0], node, depth + 1))
                return false;
            Binding binding;
            uint32_t sink{};
            if (named) {
                uint32_t length{}, capacity{}, data{};
                if (!Read(node + 0x20, length) || !Read(node + 0x24, capacity) || length > 128 || length > capacity)
                    return false;
                auto address = node + 0x10;
                if (capacity >= 16) {
                    if (!Read(address, data))
                        return false;
                    address = data;
                }
                binding.name.resize(length);
                if (length && !Read(address, binding.name.data(), length))
                    return false;
                for (unsigned char c : binding.name)
                    if (c < 32 || c > 126)
                        return false;
            } else if (!Read(node + 0x10, binding.id))
                return false;
            if (!Read(node + (named ? 0x28 : 0x14), sink) || !readSink(sink, binding))
                return false;
            bindings.push_back(std::move(binding));
            return walk(child[2], node, depth + 1);
        };
        if (!walk(links[1], head, 0) || seen.size() != size)
            return false;
        for (size_t i = 1; i < bindings.size(); ++i)
            if (named ? bindings[i].name < bindings[i - 1].name : bindings[i].id < bindings[i - 1].id)
                return false;
        if (!size)
            return links[0] == head && links[1] == head && links[2] == head;
        auto extreme = [&](uintptr_t node, unsigned side) {
            for (unsigned count = 0; count <= size; ++count) {
                uint32_t next{};
                if (!Read(node + side * 4, next))
                    break;
                if (next == head)
                    return node;
                node = next;
            }
            return uintptr_t{};
        };
        return links[0] == extreme(links[1], 0) && links[2] == extreme(links[1], 2);
    };
    auto dispatcher = object + 0x12c;
    if (!tree(dispatcher + 4, false, result.numeric))
        ++result.errors;
    if (!tree(dispatcher + 12, true, result.named))
        ++result.errors;
    uint32_t list[3]{};
    if (!Read(dispatcher + 20, list, sizeof(list)) || list[0] > list[1] || list[1] > list[2] ||
        (list[1] - list[0]) % 4 || list[2] - list[0] > 128 * 4)
        ++result.errors;
    else
        for (auto item = list[0]; item < list[1]; item += 4) {
            uint32_t sink{};
            Binding binding;
            if (!Read(item, sink) || !readSink(sink, binding)) {
                ++result.errors;
                break;
            }
            result.listeners.push_back(binding);
        }
    result.complete = result.errors == 0;
    return result;
}
} // namespace txevents
