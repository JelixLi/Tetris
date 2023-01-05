/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <atomic>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "tensorflow/core/framework/allocator.h"
#include "tensorflow/core/framework/allocator_registry.h"
#include "tensorflow/core/framework/tracking_allocator.h"
#include "tensorflow/core/lib/strings/strcat.h"
#include "tensorflow/core/lib/strings/stringprintf.h"
#include "tensorflow/core/platform/mem.h"
#include "tensorflow/core/platform/mutex.h"
#include "tensorflow/core/platform/types.h"

namespace tensorflow {

// If true, cpu allocator collects more stats.
static bool cpu_allocator_collect_stats = false;

void EnableCPUAllocatorStats() { cpu_allocator_collect_stats = true; }
void DisableCPUAllocatorStats() { cpu_allocator_collect_stats = false; }
bool CPUAllocatorStatsEnabled() { return cpu_allocator_collect_stats; }

static const int kMaxTotalAllocationWarnings = 1;

static const int kMaxSingleAllocationWarnings = 5;

// If cpu_allocator_collect_stats is true, warn when the total allocated memory
// exceeds this threshold.
static const double kTotalAllocationWarningThreshold = 0.5;

// Individual allocations large than this amount will trigger a warning.
static const double kLargeAllocationWarningThreshold = 0.1;

// Cache first invocation to port::AvailableRam, as it can be expensive.
static int64_t LargeAllocationWarningBytes() {
  static int64_t value = static_cast<int64>(port::AvailableRam() *
                                            kLargeAllocationWarningThreshold);
  return value;
}

static int64_t TotalAllocationWarningBytes() {
  static int64_t value = static_cast<int64>(port::AvailableRam() *
                                            kTotalAllocationWarningThreshold);
  return value;
}

namespace {

// A default Allocator for CPU devices.  ProcessState::GetCPUAllocator() will
// return a different version that may perform better, but may also lack the
// optional stats triggered by the functions above.  TODO(tucker): migrate all
// uses of cpu_allocator() except tests to use ProcessState instead.
class CPUAllocator : public Allocator {
 public:
  CPUAllocator()
      : single_allocation_warning_count_(0),
        total_allocation_warning_count_(0) {}

  ~CPUAllocator() override {}

  string Name() override { return "cpu"; }

  void* AllocateRaw(size_t alignment, size_t num_bytes) override {
    
    if (num_bytes > static_cast<size_t>(LargeAllocationWarningBytes()) &&
        single_allocation_warning_count_ < kMaxSingleAllocationWarnings) {
      ++single_allocation_warning_count_;
      LOG(WARNING) << "Allocation of " << num_bytes << " exceeds "
                   << 100 * kLargeAllocationWarningThreshold
                   << "% of free system memory.";
    }

    void* p = port::AlignedMalloc(num_bytes, alignment);
    if (cpu_allocator_collect_stats) {
      const std::size_t alloc_size = port::MallocExtension_GetAllocatedSize(p);
      mutex_lock l(mu_);
      ++stats_.num_allocs;
      stats_.bytes_in_use += alloc_size;
      stats_.peak_bytes_in_use =
          std::max<int64>(stats_.peak_bytes_in_use, stats_.bytes_in_use);
      stats_.largest_alloc_size =
          std::max<int64>(stats_.largest_alloc_size, alloc_size);

      if (stats_.bytes_in_use > TotalAllocationWarningBytes() &&
          total_allocation_warning_count_ < kMaxTotalAllocationWarnings) {
        ++total_allocation_warning_count_;
        LOG(WARNING) << "Total allocated memory " << stats_.bytes_in_use
                     << "exceeds " << 100 * kTotalAllocationWarningThreshold
                     << "% of free system memory";
      }
    }
    return p;
  }

  void DeallocateRaw(void* ptr) override {
    if (cpu_allocator_collect_stats) {
      const std::size_t alloc_size =
          port::MallocExtension_GetAllocatedSize(ptr);
      mutex_lock l(mu_);
      stats_.bytes_in_use -= alloc_size;
    }
    
    port::AlignedFree(ptr);
  }

  absl::optional<AllocatorStats> GetStats() override {
    mutex_lock l(mu_);
    return stats_;
  }

  void ClearStats() override {
    mutex_lock l(mu_);
    stats_.num_allocs = 0;
    stats_.peak_bytes_in_use = stats_.bytes_in_use;
    stats_.largest_alloc_size = 0;
  }

  size_t AllocatedSizeSlow(const void* ptr) const override {
    return port::MallocExtension_GetAllocatedSize(ptr);
  }

 private:
  mutex mu_;
  AllocatorStats stats_ TF_GUARDED_BY(mu_);

  // Use <atomic> for single allocations to avoid mutex contention when
  // statistics are disabled.
  std::atomic<int> single_allocation_warning_count_;
  int total_allocation_warning_count_ TF_GUARDED_BY(mu_);

  TF_DISALLOW_COPY_AND_ASSIGN(CPUAllocator);
};

class CPUAllocatorFactory : public AllocatorFactory {
 public:
  Allocator* CreateAllocator() override { return new CPUAllocator; }

  SubAllocator* CreateSubAllocator(int numa_node) override {
    return new CPUSubAllocator(new CPUAllocator);
  }

 private:
  class CPUSubAllocator : public SubAllocator {
   public:
    explicit CPUSubAllocator(CPUAllocator* cpu_allocator)
        : SubAllocator({}, {}), cpu_allocator_(cpu_allocator) {}

    void* Alloc(size_t alignment, size_t num_bytes) override {
      return cpu_allocator_->AllocateRaw(alignment, num_bytes);
    }

    void Free(void* ptr, size_t num_bytes) override {
      cpu_allocator_->DeallocateRaw(ptr);
    }

   private:
    CPUAllocator* cpu_allocator_;
  };
};

REGISTER_MEM_ALLOCATOR("DefaultCPUAllocator", 100, CPUAllocatorFactory);
}  // namespace


namespace {


// flock: https://blog.csdn.net/sin0803/article/details/38389701

void err_sys(const char *s) {
    perror(s);
    exit(1);
}

class FileLock {
public:
    explicit FileLock(std::string lock_file);

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    ~FileLock();

    void lock();
    void unlock();

    void increment_seq(pid_t pid,char *argv[]);

private:
    int Fcntl(int fd, int cmd, void *arg);
    void Close(int fd);
    int try_create_lock_file(const char *lock_file);
    off_t Lseek(int fd, off_t offset, int whence);
    ssize_t Read(int fd, void *ptr, off_t nbytes);
    void Write(int fd, void *ptr, off_t nbytes);

    void lock(int fd);
    void unlock(int fd);

    int fd; //lock file fd
};

FileLock::FileLock(std::string lock_file) {
    fd = try_create_lock_file(lock_file.c_str());
}

FileLock::~FileLock() {
    Close(fd);
}

void FileLock::lock() {
    lock(fd);
}

void FileLock::unlock() {
    unlock(fd);
}

int FileLock::Fcntl(int fd, int cmd, void *arg) {
    int	n;
    if ( (n = fcntl(fd, cmd, arg)) == -1)
        err_sys("fcntl error");
    return n;
}

void FileLock::lock(int fd) {
    struct flock lock;

    lock.l_type = F_WRLCK; // A write lock is requested.
    lock.l_whence = SEEK_SET; // The relative offset is measured from the start of the file.
    lock.l_start = 0; // Defines the relative offset in bytes, measured from the starting point in the l_whence field.
    lock.l_len = 0; // (write lock entire file here) Specifies the number of consecutive bytes to be locked.

    Fcntl(fd,F_SETLKW,&lock); // block until lock acquired.
}

void FileLock::unlock(int fd) {
    struct flock lock;

    lock.l_type = F_UNLCK; // Unlock. An existing lock is to be removed.
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    Fcntl(fd,F_SETLK,&lock);
}

void FileLock::Close(int fd) {
	if (close(fd) == -1)
		err_sys("file close error");
}

int FileLock::try_create_lock_file(const char *lock_file) {
    #define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
    int tempfd = open(lock_file,O_RDWR | O_CREAT | O_EXCL,FILE_MODE);
    if(tempfd < 0) {
        if(errno != EEXIST) {
            err_sys("open error for lock file");
        } else {
            tempfd = open(lock_file,O_RDWR,FILE_MODE);
            if(tempfd < 0) {
                err_sys("open error for lock file");
            }
        }
    }
    return tempfd;
}

off_t FileLock::Lseek(int fd, off_t offset, int whence) {
	off_t	pos;
	if ( (pos = lseek(fd, offset, whence)) == (off_t) -1)
		err_sys("lseek error");
	return pos;
}

ssize_t FileLock::Read(int fd, void *ptr, off_t nbytes) {
	ssize_t		n;
	if ( (n = read(fd, ptr, nbytes)) == -1)
		err_sys("read error");
	return n;
}


void FileLock::Write(int fd, void *ptr, off_t nbytes)
{
	if (write(fd, ptr, nbytes) != nbytes)
		err_sys("write error");
}

void FileLock::increment_seq(pid_t pid,char *argv[]) {
    #define	MAXLINE		10	/* max text line length */
    char line[MAXLINE+1];
    long seqno;
    Lseek(fd,0L,SEEK_SET); // rewind before read
    ssize_t n = Read(fd,line,MAXLINE);
    line[n] = '\0'; // null terminate for sscanf
    n = sscanf(line,"%ld\n",&seqno);
    printf("%s: pid = %ld,seq# = %ld\n",argv[0],(long)pid,seqno);
    seqno++; // increment sequence number
    snprintf(line,sizeof(line),"%ld\n",seqno);
    Lseek(fd,0L,SEEK_SET); // rewind before write
    Write(fd,line,strlen(line));
}

void file_lock_test(char *argv[]) {
    pid_t pid;
    for(int i=0;i<2;i++) {
        pid = fork();
    }

    std::string lock_file = "/home/tank/lijie/serving_locks/test.lock";
    pid = getpid();
    FileLock f_lock(lock_file);

    for(int i=0;i<5;i++) {
        f_lock.lock(); // lock the file
        f_lock.increment_seq(pid,argv);
        f_lock.unlock(); // unlock the file
    }
}

template<class T>
class MMapAllocator {
public:
    explicit MMapAllocator(std::string lock_file);

    MMapAllocator(const MMapAllocator<T>&) = delete;
    MMapAllocator& operator=(const MMapAllocator<T>&) = delete;

    T* allocate(std::string mem_id_str,off_t mem_size,bool& mem_not_exist);

    ~MMapAllocator();
private:

    FileLock *f_lock;
    void *_base_ptr;
    off_t _size;
};

void* mmap_shm_open(std::string mmap_file) {
    int fd = open(mmap_file.c_str(),O_RDWR);
    if(fd == -1) {
        err_sys("open error for mmap file");
    }
    struct stat statbuf;
    if(fstat(fd,&statbuf) != 0) {
        err_sys("fstat error for mmap file");
    }
    void *ptr = mmap(0,statbuf.st_size,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
    if(ptr == MAP_FAILED) {
        err_sys("mmap error for mmap file");
    }
    if(close(fd) != 0) {
        err_sys("close failed for mmap file");
    }
    return ptr;
}

bool mmap_file_exist(std::string mmap_file) {
    if(access(mmap_file.c_str(),F_OK)==-1) return false;
    return true;
}

void create_mmap_file(FileLock *f_lock,std::string mmap_file,off_t size) {
    f_lock->lock();
    #define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
    int tempfd = open(mmap_file.c_str(),O_RDWR | O_CREAT | O_EXCL,FILE_MODE);
    if(tempfd < 0 && errno != EEXIST) {
        err_sys("open error for mmap file");
    }
    if(tempfd > 0) {
        if(ftruncate(tempfd,size) != 0) {
            err_sys("ftruncate failed for mmap file"); 
        }
        
        // void* ptr = nullptr;
        // if (size > 0) {
        //     ptr = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, tempfd, 0);
        //     if(ptr == MAP_FAILED) {
        //         err_sys("mmap failed in creating mmap file");
        //     }
        //     memset(ptr, 0, size);
        // }

        if(close(tempfd) != 0) {
            err_sys("close failed for mmap file");
        }
    }
    f_lock->unlock();
}

void create_mmap_file_1(FileLock *f_lock,std::string mmap_file,off_t size,bool& mem_not_exist) {
    f_lock->lock();
    #define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
    int tempfd = open(mmap_file.c_str(),O_RDWR | O_CREAT | O_EXCL,FILE_MODE);
    if(tempfd < 0) {
        if(errno != EEXIST) {
            err_sys("open error for mmap file");
        } else {
          mem_not_exist = false;
        }
    } else if(tempfd > 0) {
        if(ftruncate(tempfd,size) != 0) {
            err_sys("ftruncate failed for mmap file"); 
        }

        if(close(tempfd) != 0) {
            err_sys("close failed for mmap file");
        }
    }
    f_lock->unlock();
}

template<typename T>
T* MMapAllocator<T>::allocate(std::string mem_id_str,off_t mem_size,bool& mem_not_exist) {
    // std::string mmap_file = "/dev/shm/serving_memorys/" + mem_id_str;
    // mem_size = mem_size * sizeof(T);
    // mem_not_exist = false;
    // if(!mmap_file_exist(mem_id_str)) {
    //     create_mmap_file(f_lock,mmap_file,mem_size);
    //     mem_not_exist = true;
    // }
    // _base_ptr = mmap_shm_open(mmap_file);
    // _size = mem_size;
    // return static_cast<T*>(_base_ptr);
  
    std::string mmap_file = "/dev/shm/serving_memorys/" + mem_id_str;
    mem_size = mem_size * sizeof(T);
    if(mmap_file_exist(mem_id_str)) {
      mem_not_exist = false;
    } else {
      create_mmap_file_1(f_lock,mmap_file,mem_size,mem_not_exist);
    }
    _base_ptr = mmap_shm_open(mmap_file);
    _size = mem_size;
    return static_cast<T*>(_base_ptr);
}

template<typename T>
MMapAllocator<T>::MMapAllocator(std::string lock_file) {
    f_lock = new FileLock(lock_file);
    _size = 0;
    _base_ptr = nullptr;
}

template<typename T>
MMapAllocator<T>::~MMapAllocator() {
    // if(_base_ptr != nullptr && _size > 0) {
    //     munmap(_base_ptr,_size);
    // }
    delete f_lock;
}

class CPUMmapAllocator : public Allocator {
 public:
  explicit CPUMmapAllocator(std::string mmap_id):mem_not_exist_(true) { this->mmap_id_ = mmap_id; }

  ~CPUMmapAllocator() override { 

  }

  string Name() override { return "cpu_mmap"; }

  bool MemNotExist() override { return mem_not_exist_; }

  void* AllocateRaw(size_t alignment, size_t num_bytes) override {
    // std::stringstream ss;
    // ss<<num_bytes;
    // std::string num_bytes_str = ss.str();
    // std::string mmap_id = this->mmap_id_ + num_bytes_str;

    // std::string lock_file = "/home/tank/lijie/serving_locks/" + mmap_id;
    // std::string memory_id_str = mmap_id;

    std::string lock_file = "/home/tank/lijie/serving_locks/" + this->mmap_id_;
    std::string memory_id_str = this->mmap_id_;

    // std::string lock_file = "/home/tank/lijie/serving_locks/" + this->mmap_id_;
    // std::string memory_id_str = this->mmap_id_;

    MMapAllocator<char> mmap_alloc(lock_file);
    void *p = (void*)mmap_alloc.allocate(memory_id_str,num_bytes,this->mem_not_exist_);
    assert(reinterpret_cast<intptr_t>(p) % 64 == 0);
    // void* p = port::AlignedMalloc(num_bytes, alignment);
    return p;
  }

  void DeallocateRaw(void* ptr) override {
    // port::AlignedFree(ptr);
  }

 private:
  std::string mmap_id_;
  bool mem_not_exist_;
  TF_DISALLOW_COPY_AND_ASSIGN(CPUMmapAllocator);
};

class CPUMmapAllocatorFactory : public AllocatorFactory {
 public:
  Allocator* CreateAllocator() override { return new CPUMmapAllocator(""); }

  Allocator* CreateMmapAllocator(std::string mmap_id) { return new CPUMmapAllocator(mmap_id); }

  SubAllocator* CreateSubAllocator(int numa_node) override {
    return new CPUMmapSubAllocator(new CPUMmapAllocator(""));
  }

 private:
  class CPUMmapSubAllocator : public SubAllocator {
   public:
    explicit CPUMmapSubAllocator(CPUMmapAllocator* cpu_allocator)
        : SubAllocator({}, {}), cpu_allocator_(cpu_allocator) {}

    void* Alloc(size_t alignment, size_t num_bytes) override {
      return cpu_allocator_->AllocateRaw(alignment, num_bytes);
    }

    void Free(void* ptr, size_t num_bytes) override {
      cpu_allocator_->DeallocateRaw(ptr);
    }

   private:
    CPUMmapAllocator* cpu_allocator_;
  };
};

REGISTER_MEM_ALLOCATOR("MmapCPUAllocator", 0, CPUMmapAllocatorFactory);

} // namespace

}  // namespace tensorflow
