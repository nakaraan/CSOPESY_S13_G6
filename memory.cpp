#include "memory.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>

std::unique_ptr<Memory> globalMemory = nullptr;

// initializes the memory manager along with the backing store file
Memory::Memory(size_t totalMemory, const std::string& backingStore)
    : totalMemorySize(totalMemory), backingStoreFile(backingStore), currentTime(0) {
    numFrames = static_cast<size_t>(totalMemorySize / PAGE_SIZE);
    if (numFrames == 0) numFrames = 1;
    frames.resize(numFrames);
    for (size_t i = 0; i < numFrames; ++i) {
        frames[i].frameId = static_cast<int>(i);
        frames[i].processId = -1; // where -1 indicates unallocated frame
        frames[i].pageNumber = 0;
        frames[i].isModified = false;
        frames[i].lastAccessTime = 0;
        freeFrameList.push_back(static_cast<int>(i));
    }
    stats.totalMemory = totalMemorySize;
    stats.freeMemory = numFrames * PAGE_SIZE;
}

// cleans up memory manager resources 
Memory::~Memory() {
}

void initializeMemory(size_t totalMemory) {
    globalMemory = std::make_unique<Memory>(totalMemory);
}

// true = successful, false = process already exists
bool Memory::allocateProcess(int processId, size_t processMemorySize) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    size_t pagesNeeded = (processMemorySize + PAGE_SIZE - 1) / PAGE_SIZE;
    pageTables[processId] = std::vector<PageTableEntry>(pagesNeeded);
    for (size_t i = 0; i < pagesNeeded; ++i) {
        pageTables[processId][i].pageNumber = i;
        pageTables[processId][i].isValid = false; // means that page is not yet in memory
        pageTables[processId][i].frameNumber = -1;
        pageTables[processId][i].isModified = false;
    }
    return true;
}

void Memory::deallocateProcess(int processId) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    auto it = pageTables.find(processId);
    if (it == pageTables.end()) return; // process not found
    // frees the frames usedby the process
    for (auto &pte : it->second) {
        if (pte.isValid && pte.frameNumber >= 0 && static_cast<size_t>(pte.frameNumber) < frames.size()) {
            int fi = pte.frameNumber;
            // resets the frame info to a free state
            frames[fi].processId = -1;
            frames[fi].isModified = false;
            frames[fi].lastAccessTime = 0;
            freeFrameList.push_back(fi);
            stats.freeMemory += PAGE_SIZE;
        }
    }
    pageTables.erase(it);
}

// -1 if no suitable frame found
int Memory::findOldestFrameLRU() {
    uint64_t oldest = UINT64_MAX;
    int oldestIndex = -1;
    // loops to find the frame with oldest access time
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].processId >= 0) {
            if (frames[i].lastAccessTime < oldest) {
                oldest = frames[i].lastAccessTime;
                oldestIndex = static_cast<int>(i);
            }
        }
    }
    return oldestIndex;
}

void Memory::removePage(int frameIndex) {
    if (frameIndex < 0 || static_cast<size_t>(frameIndex) >= frames.size()) return; // frame is already free
    Frame &f = frames[frameIndex];
    if (f.processId < 0) return;
    int pid = f.processId;
    size_t pnum = f.pageNumber;
    auto pit = pageTables.find(pid); // invalidates the entry for the removed page
    if (pit != pageTables.end()) {
        if (pnum < pit->second.size()) {
            PageTableEntry &pageEntry = pit->second[pnum];
            if (pageEntry.isValid && pageEntry.frameNumber == frameIndex) {
                if (f.isModified) {
                    // write back
                    std::vector<uint8_t> dummy(PAGE_SIZE, 0);
                    writePageToBackingStore(pid, pnum, dummy);
                    stats.numPagedOut++;
                }
                pageEntry.isValid = false;
                pageEntry.frameNumber = -1;
                pageEntry.isModified = false;
            }
        }
    }
    f.processId = -1;
    f.isModified = false;
    f.lastAccessTime = 0;
    freeFrameList.push_back(frameIndex);
    stats.freeMemory += PAGE_SIZE;
}

void Memory::loadPage(int processId, size_t pageNumber, int frameIndex) {
    (void)processId; (void)pageNumber; (void)frameIndex;
    stats.numPagedIn++;
}

void Memory::writePageToBackingStore(int processId, size_t pageNumber, const std::vector<uint8_t>& pageData) {
    (void)processId; (void)pageNumber; (void)pageData;
}

std::vector<uint8_t> Memory::readPageFromBackingStore(int processId, size_t pageNumber) {
    (void)processId; (void)pageNumber;
    return std::vector<uint8_t>(PAGE_SIZE, 0);
}

bool Memory::accessMemory(int processId, size_t virtualAddress, bool isWrite) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    currentTime++; // increments the time for LRU demand paging
    auto it = pageTables.find(processId);
    if (it == pageTables.end()) return false;
    size_t pageNumber = virtualAddress / PAGE_SIZE;
    if (pageNumber >= it->second.size()) return false;
    PageTableEntry &pageEntry = it->second[pageNumber];
    if (!pageEntry.isValid) { // for page faults (aka page not in memory)
        int frameIndex = -1;
        if (!freeFrameList.empty()) {
            frameIndex = freeFrameList.front();
            freeFrameList.pop_front();
            stats.freeMemory -= PAGE_SIZE;
        } else {
            frameIndex = findOldestFrameLRU(); // if no free frames are available, find the oldest frame using LRU
            if (frameIndex < 0) return false;
            removePage(frameIndex);
            // after removal, a free frame is available
            if (!freeFrameList.empty()) { 
                frameIndex = freeFrameList.front();
                freeFrameList.pop_front();
                stats.freeMemory -= PAGE_SIZE;
            } else {
                return false;
            }
        }
        // load page into frame
        frames[frameIndex].processId = processId;
        frames[frameIndex].pageNumber = pageNumber;
        frames[frameIndex].isModified = false;
        frames[frameIndex].lastAccessTime = currentTime;
        loadPage(processId, pageNumber, frameIndex);
        pageEntry.isValid = true;
        pageEntry.frameNumber = frameIndex;
        pageEntry.isModified = false;
    } else {
        // if there is a page hit, update the last access time for LRU
        if (pageEntry.frameNumber >= 0 && static_cast<size_t>(pageEntry.frameNumber) < frames.size()) {
            frames[pageEntry.frameNumber].lastAccessTime = currentTime;
        }
    }
    if (isWrite) {
        pageEntry.isModified = true;
        if (pageEntry.frameNumber >= 0 && static_cast<size_t>(pageEntry.frameNumber) < frames.size()) {
            frames[pageEntry.frameNumber].isModified = true;
            frames[pageEntry.frameNumber].lastAccessTime = currentTime;
        }
    }
    return true;
}

uint8_t Memory::readByte(int processId, size_t virtualAddress) {
    if (!accessMemory(processId, virtualAddress, false)) return 0;
    return 0;
}

bool Memory::writeByte(int processId, size_t virtualAddress, uint8_t value) {
    if (!accessMemory(processId, virtualAddress, true)) return false;
    (void)value;
    return true;
}

MemoryStats Memory::getStats() const {
    std::lock_guard<std::mutex> lock(memoryMutex);
    return stats;
}

void Memory::updateCpuTicks(bool isIdle) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    if (isIdle) stats.idleCpuTicks++; else stats.activeCpuTicks++;
}

// returns memory usage in bytes
size_t Memory::getProcessMemoryUsage(int processId) const {
    std::lock_guard<std::mutex> lock(memoryMutex);
    auto it = pageTables.find(processId);
    if (it == pageTables.end()) return 0;
    return it->second.size() * PAGE_SIZE;
}

std::vector<std::pair<int, size_t>> Memory::getAllProcessMemoryInfo() const {
    std::lock_guard<std::mutex> lock(memoryMutex);
    std::vector<std::pair<int, size_t>> out;
    for (const auto &kv : pageTables) { // calculate memory usage for stats by counting valid pages
        out.emplace_back(kv.first, kv.second.size() * PAGE_SIZE);
    }
    return out;
}

bool Memory::hasProcess(int processId) const {
    std::lock_guard<std::mutex> lock(memoryMutex);
    return pageTables.find(processId) != pageTables.end();
}

void Memory::printMemoryState() const {
    std::lock_guard<std::mutex> lock(memoryMutex);
    std::cout << "Memory: total=" << stats.totalMemory << " free=" << stats.freeMemory << " frames=" << numFrames << "\n";
}
