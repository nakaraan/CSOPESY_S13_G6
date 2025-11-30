#include "memory.h"
#include "globals.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>

std::unique_ptr<Memory> globalMemory = nullptr;

// ================= Backing Store Persistence Toggle =================
// Set this to true to keep appending to the backing store across runs.
// Set to false to truncate (clear) the backing store each time memory is initialized.
// User can toggle this single boolean. No config.txt entry required.
static const bool BACKING_STORE_PERSIST = false;
// Optional maximum backing store size (in bytes) before auto-truncation (only when persisting)
static const size_t BACKING_STORE_MAX_SIZE = 5 * 1024 * 1024; // 5 MiB cap

// Returns actual page size from config (mem_per_frame is in KB, convert to bytes)
inline size_t getPageSize() {
    return (mem_per_frame > 0) ? (mem_per_frame * 1024) : 1024;
}

// initializes the memory manager along with the backing store file
Memory::Memory(size_t totalMemory, const std::string& backingStore)
    : totalMemorySize(totalMemory), backingStoreFile(backingStore), currentTime(0) {
    size_t pageSize = getPageSize();
    numFrames = static_cast<size_t>(totalMemorySize / pageSize);
    // Enforce single-process residency: cap total frames to per-process capacity
    // Frames per process derived from max_mem_per_proc (KB) and page size (bytes)
    if (max_mem_per_proc > 0) {
        size_t framesPerProc = (max_mem_per_proc * 1024) / pageSize;
        if (framesPerProc == 0) framesPerProc = 1;
        if (numFrames > framesPerProc) {
            numFrames = framesPerProc;
        }
    }
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
    stats.usedMemory = 0;
    stats.freeMemory = totalMemorySize; // will be recomputed on getStats

    // -------- Backing Store Initialization --------
    if (!backingStoreFile.empty()) {
        bool needHeader = false;
        if (BACKING_STORE_PERSIST) {
            // If persisting, check size; rotate/truncate if too large
            std::ifstream ifs(backingStoreFile, std::ios::binary | std::ios::ate);
            if (ifs.good()) {
                auto size = static_cast<size_t>(ifs.tellg());
                if (size > BACKING_STORE_MAX_SIZE) {
                    // Truncate oversized file
                    std::ofstream trunc(backingStoreFile, std::ios::trunc);
                    needHeader = true;
                } else if (size == 0) {
                    needHeader = true; // Empty file
                }
            } else {
                // File doesn't exist yet
                needHeader = true;
            }
            ifs.close();
            if (needHeader) {
                std::ofstream ofs(backingStoreFile, std::ios::app);
                if (ofs.good()) {
                    ofs << "# CSOPESY Backing Store\n";
                    ofs << "# Format: PID PAGE_NUM DATA\n";
                }
            }
        } else {
            // Not persisting: always truncate and write header
            std::ofstream ofs(backingStoreFile, std::ios::trunc);
            if (ofs.good()) {
                ofs << "# CSOPESY Backing Store\n";
                ofs << "# Format: PID PAGE_NUM DATA\n";
            }
        }
    }
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
    size_t pagesNeeded = (processMemorySize + getPageSize() - 1) / getPageSize();
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
            frames[fi].processId = -1;
            frames[fi].isModified = false;
            frames[fi].lastAccessTime = 0;
            freeFrameList.push_back(fi);
            if (stats.usedMemory >= getPageSize()) stats.usedMemory -= getPageSize();
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
                // Count eviction
                stats.numPagedOut++;
                // Write only if modified or not yet present in store
                uint64_t key = (static_cast<uint64_t>(pid) << 32) | static_cast<uint64_t>(pnum);
                if (frames[frameIndex].isModified || backingStorePresence.find(key) == backingStorePresence.end()) {
                    std::vector<uint8_t> dummy(getPageSize(), 0);
                    writePageToBackingStore(pid, pnum, dummy);
                    backingStorePresence.insert(key);
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
    if (stats.usedMemory >= getPageSize()) stats.usedMemory -= getPageSize();
}

void Memory::loadPage(int processId, size_t pageNumber, int frameIndex) {
    (void)processId; (void)pageNumber; (void)frameIndex;
    stats.numPagedIn++;
    stats.usedMemory += getPageSize();
}

// helper: encode bytes to hex string (compact text)
static std::string toHex(const std::vector<uint8_t>& data) {
    static const char* hexdigits = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out.push_back(hexdigits[b >> 4]);
        out.push_back(hexdigits[b & 0x0F]);
    }
    return out;
}

void Memory::writePageToBackingStore(int processId, size_t pageNumber, const std::vector<uint8_t>& pageData) {
    std::ofstream ofs(backingStoreFile, std::ios::app);
    if (!ofs) return;
    ofs << processId << " " << pageNumber << " ";
    ofs << toHex(pageData) << "\n";
}

std::vector<uint8_t> Memory::readPageFromBackingStore(int processId, size_t pageNumber) {
    (void)processId; (void)pageNumber;
    return std::vector<uint8_t>(getPageSize(), 0);
}

bool Memory::accessMemory(int processId, size_t virtualAddress, bool isWrite) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    currentTime++; // increments the time for LRU demand paging
    auto it = pageTables.find(processId);
    if (it == pageTables.end()) return false;
    size_t pageNumber = virtualAddress / getPageSize();
    if (pageNumber >= it->second.size()) return false;
    PageTableEntry &pageEntry = it->second[pageNumber];
    if (!pageEntry.isValid) { // for page faults (aka page not in memory)
        int frameIndex = -1;
        if (!freeFrameList.empty()) {
            frameIndex = freeFrameList.front();
            freeFrameList.pop_front();
        } else {
            frameIndex = findOldestFrameLRU(); // if no free frames are available, find the oldest frame using LRU
            if (frameIndex < 0) return false;
            removePage(frameIndex);
            // after removal, a free frame is available
            if (!freeFrameList.empty()) { 
                frameIndex = freeFrameList.front();
                freeFrameList.pop_front();
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
    MemoryStats copy = stats;
    // Recompute free memory to avoid drift
    if (copy.totalMemory >= copy.usedMemory) copy.freeMemory = copy.totalMemory - copy.usedMemory; else copy.freeMemory = 0;
    return copy;
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
    return it->second.size() * getPageSize();
}

std::vector<std::pair<int, size_t>> Memory::getAllProcessMemoryInfo() const {
    std::lock_guard<std::mutex> lock(memoryMutex);
    std::vector<std::pair<int, size_t>> out;
    for (const auto &kv : pageTables) { // calculate memory usage for stats by counting valid pages
        out.emplace_back(kv.first, kv.second.size() * getPageSize());
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
