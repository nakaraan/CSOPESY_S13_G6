#ifndef CSOPESY_MEMORY_H
#define CSOPESY_MEMORY_H

#include <vector>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <string>
#include <cstdint>
#include <memory>

// Page size in bytes (1KB)
const size_t PAGE_SIZE = 1024;

// Single page table entry, for process
struct PageTableEntry {
    size_t pageNumber;
    bool isValid;
    int frameNumber;
    bool isModified;
};

// Single frame
struct Frame {
    int frameId;
    int processId; // -1 if free
    size_t pageNumber;
    bool isModified;
    uint64_t lastAccessTime;
};

// Memory usage and performance statistics
struct MemoryStats {
    size_t totalMemory = 0;
    size_t usedMemory = 0;
    size_t freeMemory = 0;
    size_t numPagedIn = 0;
    size_t numPagedOut = 0;
    uint64_t idleCpuTicks = 0;
    uint64_t activeCpuTicks = 0;
};

// Manages virtual memory with paging and LRU page replacement
class Memory {
public:
    // Constructor: total memory in bytes, optional backing store file path
    Memory(size_t totalMemory, const std::string& backingStore = "csopesy-backing-store.txt");
    ~Memory();

    bool allocateProcess(int processId, size_t processMemorySize);
    void deallocateProcess(int processId);

    bool accessMemory(int processId, size_t virtualAddress, bool isWrite);
    uint8_t readByte(int processId, size_t virtualAddress);
    bool writeByte(int processId, size_t virtualAddress, uint8_t value);

    MemoryStats getStats() const;
    void updateCpuTicks(bool isIdle);

    size_t getProcessMemoryUsage(int processId) const;
    std::vector<std::pair<int, size_t>> getAllProcessMemoryInfo() const;
    bool hasProcess(int processId) const;
    void printMemoryState() const;

private:
    int findOldestFrameLRU();
    void removePage(int frameIndex);
    void loadPage(int processId, size_t pageNumber, int frameIndex);
    void writePageToBackingStore(int processId, size_t pageNumber, const std::vector<uint8_t>& pageData);
    std::vector<uint8_t> readPageFromBackingStore(int processId, size_t pageNumber);

    size_t totalMemorySize;
    size_t numFrames;
    std::vector<Frame> frames;
    std::deque<int> freeFrameList;
    std::unordered_map<int, std::vector<PageTableEntry>> pageTables;
    std::string backingStoreFile;
    uint64_t currentTime;
    MemoryStats stats;
    mutable std::mutex memoryMutex;
};

// Global memory instance
extern std::unique_ptr<Memory> globalMemory;

// Initialize global memory instance
void initializeMemory(size_t totalMemory);

#endif // CSOPESY_MEMORY_H
