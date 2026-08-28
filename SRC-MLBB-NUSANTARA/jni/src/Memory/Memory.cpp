#include "Memory.h"

//#include "syscall/secure_syscall.h"
//#include "macros.h"
#include <cstdio>
#include <dirent.h>
#include <iomanip>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/system_properties.h>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include "zip/zip.h""

#if defined(__arm__)
#define process_vm_readv_syscall 376
#define process_vm_writev_syscall 377
#define pwrite64_syscall 181
#define pread64_syscall 180
#elif defined(__aarch64__)
#define process_vm_readv_syscall 270
#define process_vm_writev_syscall 271
#define pwrite64_syscall 67
#define pread64_syscall 68
#elif defined(__i386__)
#define process_vm_readv_syscall 347
#define process_vm_writev_syscall 348
#define pwrite64_syscall 181
#define pread64_syscall 180
#elif defined(__x86_64__)
#define process_vm_readv_syscall 310
#define process_vm_writev_syscall 311
#define pwrite64_syscall 18
#define pread64_syscall 17
#endif


ssize_t process_v(pid_t _pid, const struct iovec *_local_iov,
                  unsigned long _local_iov_count, const struct iovec *_remote_iov,
                  unsigned long _remote_iov_count, unsigned long _flags, bool iswrite)
{
    return syscall((iswrite ? process_vm_writev_syscall : process_vm_readv_syscall), _pid, _local_iov, _local_iov_count, _remote_iov, _remote_iov_count, _flags);
}


bool pvm(pid_t pid, void* address, void* buffer, size_t size, bool isWrite) {
    if (pid < 10) return false;
    struct iovec local[1];
    struct iovec remote[1];
    local[0].iov_base = buffer;
    local[0].iov_len = size;
    remote[0].iov_base = address;
    remote[0].iov_len = size;
    ssize_t bytes = process_v(pid, local, 1, remote, 1, 0, isWrite);
    return bytes == size;
}

int is_page_present(unsigned long vaddr) {
    int fh = 1;
    uintptr_t pageSize = sysconf(_SC_PAGE_SIZE);
    if (pageSize == -1) {
        //perror("Error getting page size");
        return -1;
    }
    uintptr_t v_pageIndex = vaddr / pageSize;
    uintptr_t pfn_item_offset = v_pageIndex * sizeof(uint64_t);
    uintptr_t page_offset = vaddr % pageSize;
    uint64_t item = 0;
    char filename[32];
    snprintf(filename, sizeof(filename), "/proc/%d/pagemap", Memory::g_pid);
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        //perror("Error opening pagemap file");
        return -1;
    }
    if (lseek(fd, pfn_item_offset, SEEK_SET) < 0) {
        //perror("Error seeking in pagemap file");
        close(fd);
        return -1;
    }

    if (read(fd, &item, sizeof(uint64_t)) != sizeof(uint64_t)) {
        //perror("Error reading from pagemap file");
        close(fd);
        return -1;
    }
    if (0 == (item & (1ULL << 63))) {
        fh = 0;
        //printf("Page is not present\n");
        close(fd);
        return 0;
    }
    close(fd);
    return 1;
}

pid_t Memory::FindPid(const char* package_name) {
    pid_t pid = -1;
    char cmd_pidof[69];
    sprintf(cmd_pidof, "pidof %s", package_name);
    auto str_pid = Exec(cmd_pidof);
    if (!str_pid.empty()) {
        pid = strtol(str_pid.c_str(), nullptr, 10);
        if (pid < 10) {
		    struct dirent* ptr = nullptr;
		    FILE* fp = nullptr;
		    char filepath[256];
		    char fileText[128];
            DIR* dir = opendir("/proc");
		    if (nullptr != dir) {
                while ((ptr = readdir(dir)) != nullptr) {
                    if ((strcmp(ptr->d_name, ".") == 0) || (strcmp(ptr->d_name, "..") == 0)) continue;
                    if (ptr->d_type != DT_DIR) continue;
                    sprintf(filepath, "/proc/%s/cmdline", ptr->d_name);
				    fp = fopen(filepath, "r");
                    if (nullptr == fp) continue;
                    fgets(fileText, sizeof(fileText), fp);
                    if (strcmp(fileText, package_name) == 0) {
						fclose(fp);
						break;
					}
					fclose(fp);
                }
            } else {
                closedir(dir);
                return pid;
            }
            closedir(dir);
            pid = atoi(ptr->d_name);
        }
    }
    return pid;
}

bool validateElf(uintptr_t base) {
    static unsigned char sig[5] = "\x7F\x45\x4C\x46";
    unsigned char *buff = new unsigned char[5];
    Memory::ProcessRead((void*)base, buff, 5);
    for (int i = 0; i < 4; i++) {
        if (sig[i] != buff[i]) {
            delete[] buff;
            return false;
        }
    }
    delete[] buff;
    return true;
}
    
uintptr_t Memory::GetBase(const char *lib_name, pid_t pid) {
    uintptr_t base=0;
    char map[128];
    char line[512];
    sprintf(map, "/proc/%d/maps", pid);
    FILE *f = fopen(map, "rt");
    if (!f) return base;
    while (fgets(line, sizeof line, f)) {
        uintptr_t tmpBase;
        uintptr_t tmpEnd;
        char tmpName[256];
        char tmpPerms[5]; 
        if (sscanf(line, "%" PRIXPTR "-%" PRIXPTR " %4s %*s %*s %*s %s", &tmpBase, &tmpEnd, tmpPerms, tmpName) > 0) {
            if (!strcmp(basename(tmpName), lib_name)) {
                auto perms = parsePerms(tmpPerms);
                if ((perms & Prot_READ) && (perms & Prot_PRIV) && validateElf(tmpBase)) {
                    base = tmpBase;
                }
            }
        }
    }
    fclose(f);
    return base;
}
uintptr_t Memory::GetEnd(const char *lib_name, pid_t pid) {
    uintptr_t base=0;
    char map[128];
    char line[512];
    sprintf(map, "/proc/%d/maps", pid);
    FILE *f = fopen(map, "rt");
    if (!f) return base;
    while (fgets(line, sizeof line, f)) {
        uintptr_t tmpBase;
        uintptr_t tmpEnd;
        char tmpName[256];
        char tmpPerms[5]; 
        if (sscanf(line, "%" PRIXPTR "-%" PRIXPTR " %4s %*s %*s %*s %s", &tmpBase, &tmpEnd, tmpPerms, tmpName) > 0) {
            if (!strcmp(basename(tmpName), lib_name)) {
                auto perms = parsePerms(tmpPerms);
                if ((perms & Prot_READ) && (perms & Prot_PRIV) && validateElf(tmpBase)) {
                    base = tmpBase;
                }
            }
        }
    }
    fclose(f);
    return base;
}

std::vector<PMap> Memory::GetMaps(const char *name, pid_t pid) {
    std::vector<PMap> fMaps;
    char filePath[128]{0};
    char line[512]{0};
    sprintf(filePath, "/proc/%d/maps", pid);
    FILE *fp = fopen(filePath, "r");
    if (!fp) return fMaps;
    while (fgets(line, sizeof(line), fp))
    {
        PMap map;
        map.pid = pid;
        
        char perms[5] = {0}, dev[11] = {0}, pathname[256] = {0};
        
        sscanf(line, "%llx-%llx %s %llx %s %lu %s", 
                   &map.startAddress, &map.endAddress,
                   perms, &map.offset, dev, &map.inode, pathname);
                   
        if (strstr(pathname, name)) {
            auto p = parsePerms(perms);
            if ((p & Prot_READ) && (p & Prot_PRIV)) {
                map.length = map.endAddress - map.startAddress;
                map.dev = dev;
                map.pathname = pathname;
                map.perms = p;
                fMaps.push_back(map);
            }
        }
    }
    fclose(fp);
    return fMaps;
}

uintptr_t Memory::GetBaseInSplit(const char *splitName, const char *libName, pid_t pid)
{
    uintptr_t base = 0;
    auto split_maps = GetMaps(splitName);
    
    if (split_maps.empty()) return base;
    
    auto map = split_maps.front();
    
    struct zip_t* z = zip_open(map.pathname.c_str(), 0, 'r');
    if (!z) return base;
    
    int i, n = zip_entries_total(z);
    
    for (i = 0; i < n; ++i)
    {
        zip_entry_openbyindex(z, i);
        {
            std::string name = zip_entry_name(z);
            printf("%s\n",name.c_str());
            if (strstr(name.c_str(), libName) == NULL)
                continue;

            unsigned long long data_offset = zip_entry_data_offset(z);
            for (auto& it : split_maps)
            {
                if (it.inode == map.inode && it.offset == data_offset && validateElf(it.startAddress))
                {
                    base = it.startAddress;
                    zip_entry_close(z);
                    goto end;
                }
            }
            break;
        }
        zip_entry_close(z);
    }
    
    end:
    zip_close(z);
    return base;
}

uintptr_t Memory::GetEndInSplit(const char *splitName, const char *libName, pid_t pid)
{
    uintptr_t base = 0;
    auto split_maps = GetMaps(splitName);
    
    if (split_maps.empty()) return base;
    
    auto map = split_maps.front();
    
    struct zip_t* z = zip_open(map.pathname.c_str(), 0, 'r');
    if (!z) return base;
    
    int i, n = zip_entries_total(z);
    
    for (i = 0; i < n; ++i)
    {
        zip_entry_openbyindex(z, i);
        {
            std::string name = zip_entry_name(z);
            if (strstr(name.c_str(), libName) == NULL)
                continue;

            unsigned long long data_offset = zip_entry_data_offset(z);
            for (auto& it : split_maps)
            {
                if (it.inode == map.inode && it.offset == data_offset && validateElf(it.startAddress))
                {
                    base = it.endAddress;
                    zip_entry_close(z);
                    goto end;
                }
            }
            break;
        }
        zip_entry_close(z);
    }
    
    end:
    zip_close(z);
    return base;
}
    
int Memory::ProcessRead(void *address, void *out, size_t size, pid_t pid) {
    if (pid < 1) return -1;
    if (uintptr_t(address) < 0xFFFFF) return 1;
    if (!pvm(pid, address, out, size, false)) {
        //LOGE("ProcessRead %p", address);
        return 1;
    }
    return 0;
}
int Memory::ProcessRead(uintptr_t address, void *out, size_t size, pid_t pid) {
    if (pid < 1) return -1;
    if (uintptr_t(address) < 0xFFFFF) return 1;
    if (!pvm(pid, (void*)address, out, size, false)) {
        //LOGE("ProcessRead %p", address);
        return 1;
    }
    return 0;
}

int Memory::ProcessWrite(void *address, void *data, size_t size, pid_t pid) {
    if (pid < 1) return -1;
    if (uintptr_t(address) < 0xFFFFF) return 1;
    if (!pvm(pid, address, data, size, true)) {
        //LOGE("ProcessWrite %p", address);
        return 1;
    }
    return 0;
}

std::string Memory::Exec(const char *cmd) {
   std::string res;
   FILE *pipe = popen(cmd, "r");
   if (pipe == nullptr) {
       printf("ERROR: %s\n", cmd);
       return res;
   }
   char line[512];
   while (fgets(line,sizeof line, pipe))
       res += std::string(line);
   pclose(pipe);
   return res;
}

bool Memory::IsInvalidAddress(uintptr_t address) {
    return /*is_page_present(address) != 1 ||*/ address == 0x0000000000 || address == 0 || address == 0x000;
}
bool Memory::IsInvalidAddress(void *addrs) {
    auto address = reinterpret_cast<uintptr_t>(addrs);
    return /*is_page_present(address) != 1 ||*/ address == 0x0000000000 || address == 0 || address == 0x000;
}

uintptr_t Memory::FindPattern(uintptr_t start, size_t length, const unsigned char* pattern, const char* mask) {
    size_t pos = 0;
	auto maskLength = strlen(mask) - 1;
	auto startAdress = start;
	for(auto it = startAdress; it < startAdress + length; ++it )
	{
		if (Memory::Read<unsigned char>(it) == pattern[pos] || mask[pos] == '?' )
		{
			if ( mask[pos + 1] == '\0' )
				return it - maskLength;
			pos++;
		}
		else pos = 0;
	}
	return 0;
}

ssize_t io_rw(int fd, void *buff, size_t count, off_t offset, bool iswrite)
{
    return syscall((iswrite ? pwrite64_syscall : pread64_syscall), fd, buff, count, offset);
}

namespace Memory {
    pid_t mem_pid = -1;
    int mem_fd = -1;

    bool WriteIO(uintptr_t adresss, void *write_buffer, size_t size, pid_t pid)
    {
        if (pid != mem_pid || mem_fd < 0) {
            if (pid != mem_pid && mem_fd > 0)
                close(mem_fd);
            
            char memPath[64]{0};
        	sprintf(memPath, "/proc/%d/mem", pid);
            mem_fd = open(memPath, O_RDWR);
            mem_pid = pid;
        }
        if (mem_fd < 0) {
            perror("open failed");
            return -1;
        }
        lseek(mem_fd, 0, SEEK_SET);
        auto r = pwrite64(mem_fd, write_buffer, size, adresss);
        return r == size;
    }
    
    bool ReadIO(uintptr_t adresss, void *read_buffer, size_t size, pid_t pid)
    {
        if (pid != mem_pid || mem_fd < 0) {
            if (pid != mem_pid && mem_fd > 0)
                close(mem_fd);
            
            char memPath[64]{0};
        	sprintf(memPath, "/proc/%d/mem", pid);
            mem_fd = open(memPath, O_RDWR);
            mem_pid = pid;
        }
        if (mem_fd < 0) {
            perror("open failed");
            return -1;
        }
        lseek(mem_fd, 0, SEEK_SET);
        auto r = pread64(mem_fd, read_buffer, size, adresss);
        return r == size;
    }
    
    uintptr_t IOFindPattern(uintptr_t start, size_t length, const unsigned char* pattern, const char* mask) {
        size_t pos = 0;
    	auto maskLength = strlen( mask ) - 1;
 
    	auto startAdress = start;
    	for( auto it = startAdress; it < startAdress + length; ++it )
    	{
            unsigned char ch = 0;
            ReadIO(it, (void*)&ch, sizeof(ch), g_pid);
	    	if (ch == pattern[pos] || mask[pos] == '?' )
	    	{
		    	if ( mask[pos + 1] == '\0' )
			    	return it - maskLength;
 
		    	pos++;
	    	}
	    	else pos = 0;
    	}
    	return 0;
    }
    
    void CleanIO() {
        if (mem_fd <= 0) return;
        close(mem_fd);
    }
}