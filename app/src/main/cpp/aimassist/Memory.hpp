#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/types.h>
#include <sys/uio.h>
#include <type_traits>
#include <unistd.h>

#include <dlfcn.h>

namespace aimbot::mem {

inline bool isPlausiblePointer(uintptr_t address) {
  return address >= 0x1000 && (address & 0x3) == 0;
}

// /proc/self/mem is a fallback for kernels that block process_vm_readv.
inline int procMemFd() {
  static std::once_flag once;
  static int fd = -1;
  std::call_once(once, [] {
    fd = open("/proc/self/mem", O_RDWR | O_CLOEXEC);
  });
  return fd;
}

inline bool read(uintptr_t address, void *out, size_t size) {
  if (!out || size == 0 || !isPlausiblePointer(address)) return false;

  iovec local{out, size};
  iovec remote{reinterpret_cast<void *>(address), size};
  const ssize_t copied =
      process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
  if (copied == static_cast<ssize_t>(size)) return true;

  const int fd = procMemFd();
  if (fd < 0) return false;
  const ssize_t fallback =
      pread(fd, out, size, static_cast<off_t>(address));
  return fallback == static_cast<ssize_t>(size);
}

inline bool write(uintptr_t address, const void *data, size_t size) {
  if (!data || size == 0 || !isPlausiblePointer(address)) return false;

  iovec local{const_cast<void *>(data), size};
  iovec remote{reinterpret_cast<void *>(address), size};
  const ssize_t copied =
      process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
  if (copied == static_cast<ssize_t>(size)) return true;

  const int fd = procMemFd();
  if (fd < 0) return false;
  const ssize_t fallback =
      pwrite(fd, data, size, static_cast<off_t>(address));
  return fallback == static_cast<ssize_t>(size);
}

template <typename T>
inline bool read(uintptr_t address, T &out) {
  static_assert(std::is_trivially_copyable<T>::value,
                "mem::read requires a trivial type");
  return read(address, &out, sizeof(T));
}

template <typename T>
inline bool write(uintptr_t address, const T &value) {
  static_assert(std::is_trivially_copyable<T>::value,
                "mem::write requires a trivial type");
  return write(address, &value, sizeof(T));
}

inline bool isGameAddress(const void *address) {
  if (!address) return false;
  Dl_info info{};
  if (dladdr(address, &info) == 0 || !info.dli_fname) return false;
  return std::strstr(info.dli_fname, "libminecraftpe.so") != nullptr;
}

} // namespace aimbot::mem
