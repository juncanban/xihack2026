#pragma once
#include <map>
#include <vector>
#include <string>
#include <cstring>
extern std::map<std::string, std::vector<uint8_t>> fakeFiles;
extern bool failWrites;
class File {
  std::string path; bool opened=false;
public:
  File() = default;
  File(const char* name, bool write) : path(name), opened(write || fakeFiles.count(name)) {
    if (write && failWrites) { opened=false; return; }
    if(write) fakeFiles[path].clear();
  }
  operator bool() const { return opened; }
  size_t size() const { return opened ? fakeFiles[path].size() : 0; }
  size_t read(uint8_t* dest, size_t count) {
    count=std::min(count,size()); if(count) memcpy(dest,fakeFiles[path].data(),count); return count;
  }
  size_t write(const uint8_t* src,size_t count) {
    if (!opened || failWrites) return 0;
    fakeFiles[path].assign(src,src+count); return count;
  }
  void close() { opened=false; }
};
