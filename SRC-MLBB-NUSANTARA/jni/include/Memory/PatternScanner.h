#pragma once

// refs
// https://github.com/MJx0/KittyMemoryEx/blob/main/KittyMemoryEx
// https://github.com/learn-more/findpattern-bench

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


ssize_t process_vvv(pid_t _pid, const struct iovec *_local_iov,
                  unsigned long _local_iov_count, const struct iovec *_remote_iov,
                  unsigned long _remote_iov_count, unsigned long _flags, bool iswrite)
{
    return syscall((iswrite ? process_vm_writev_syscall : process_vm_readv_syscall), _pid, _local_iov, _local_iov_count, _remote_iov, _remote_iov_count, _flags);
}


bool pvmv(void* address, void* buffer, size_t size, bool isWrite) {
    if (Memory::g_pid < 10) return false;
    struct iovec local[1];
    struct iovec remote[1];
    local[0].iov_base = buffer;
    local[0].iov_len = size;
    remote[0].iov_base = address;
    remote[0].iov_len = size;
    ssize_t bytes = process_vvv(Memory::g_pid, local, 1, remote, 1, 0, isWrite);
    return bytes > 0;
}

static bool compare(const char *data, const char *pattern, const char *mask)
{
    for (; *mask; ++mask, ++data, ++pattern)
    {
        if (*mask == 'x' && *data != *pattern)
            return false;
    }
    return !*mask;
}

static uintptr_t findInRange(const uintptr_t start, const uintptr_t end,
                             const char *pattern, const std::string &mask)
{
    const size_t scan_size = mask.length();

    if (scan_size < 1 || ((start + scan_size) > end))
        return 0;

    const size_t length = end - start;

    for (size_t i = 0; i < length; ++i)
    {
        const uintptr_t current_end = start + i + scan_size;
        if (current_end > end)
            break;

        if (!compare(reinterpret_cast<const char *>(start + i), pattern, mask.c_str()))
            continue;

        return start + i;
    }
    return 0;
}


void Trim(std::string &str)
{
    str.erase(std::remove_if(str.begin(), str.end(), [](char c)
                             { return (c == ' ' || c == '\n' || c == '\r' ||
                                           c == '\t' || c == '\v' || c == '\f'); }),
              str.end());
}

bool ValidateHex(std::string &hex)
{
    if (hex.empty())
        return false;

    if (hex.compare(0, 2, "0x") == 0)
        hex.erase(0, 2);

    Trim(hex); // first remove spaces

    if (hex.length() < 2 || hex.length() % 2 != 0)
        return false;

    for (size_t i = 0; i < hex.length(); i++)
    {
        if (!std::isxdigit((unsigned char)hex[i]))
            return false;
    }

    return true;
}

void dataFromHex(
    const std::string &in, //!< Input hex string
    void *data             //!< Data store
)
{
    size_t length = in.length();
    auto *byteData = reinterpret_cast<unsigned char *>(data);

    std::stringstream hexStringStream;
    hexStringStream >> std::hex;
    for (size_t strIndex = 0, dataIndex = 0; strIndex < length; ++dataIndex)
    {
        // Read out and convert the string two characters at a time
        const char tmpStr[3] = {in[strIndex++], in[strIndex++], 0};

        // Reset and fill the string stream
        hexStringStream.clear();
        hexStringStream.str(tmpStr);

        // Do the conversion
        int tmpValue = 0;
        hexStringStream >> tmpValue;
        byteData[dataIndex] = static_cast<unsigned char>(tmpValue);
    }
}


namespace Kernel
{
    
std::vector<uintptr_t> findBytesAll(const uintptr_t start, const uintptr_t end,
                                                     const char *bytes, const std::string &mask)
{
    std::vector<uintptr_t> local_list;

    if (start >= end || !bytes || mask.empty())
        return local_list;

    std::vector<char> buf(end - start, 0);
    if (!pvmv((void*)start, &buf[0], buf.size(), false))
    {
        printf("findBytesAll: failed to read into buffer.\n");
        return local_list;
    }

    uintptr_t curr_search_address = (uintptr_t)&buf[0];
    const size_t scan_size = mask.length();
    do
    {
        if (!local_list.empty())
            curr_search_address = local_list.back() + scan_size;

        uintptr_t found = findInRange(curr_search_address, (uintptr_t(&buf[0]) + buf.size()), bytes, mask);
        if (!found)
            break;

        local_list.push_back(found);
    } while (true);

    if (local_list.empty())
        return local_list;

    std::vector<uintptr_t> remote_list;
    for (auto &it : local_list)
    {
        remote_list.push_back((it - (uintptr_t(&buf[0]))) + start);
    }

    return remote_list;
}

uintptr_t findBytesFirst(const uintptr_t start, const uintptr_t end, const char *bytes, const std::string &mask)
{
    if (start >= end || !bytes || mask.empty())
        return 0;

    std::vector<char> buf(end - start, 0);
    if (!pvmv((void*)start, &buf[0], buf.size(), false))
    {
        printf("findBytesFirst: failed to read into buffer %p.\n", (void*)start);
        return 0;
    }

    uintptr_t local = findInRange((uintptr_t)&buf[0], (uintptr_t(&buf[0]) + buf.size()), bytes, mask);
    if (local)
        return (local - (uintptr_t(&buf[0]))) + start;
        
     
    return 0;
}

std::vector<uintptr_t> findHexAll(const uintptr_t start, const uintptr_t end, std::string hex, const std::string &mask)
{
    std::vector<uintptr_t> list;

    if (start >= end || mask.empty() || !ValidateHex(hex))
        return list;

    const size_t scan_size = mask.length();
    if ((hex.length() / 2) != scan_size)
        return list;

    std::vector<char> pattern(scan_size);
    dataFromHex(hex, &pattern[0]);

    list = findBytesAll(start, end, pattern.data(), mask);
    return list;
}

std::vector<uintptr_t> findIdaPatternAll(const uintptr_t start, const uintptr_t end, const std::string& pattern)
{
    std::vector<uintptr_t> list;

    if (start >= end)
        return list;

    std::string mask;
    std::vector<char> bytes;

    const size_t pattren_len = pattern.length();
    for (std::size_t i = 0; i < pattren_len; i++)
    {
        if (pattern[i] == ' ') continue;
		
        if (pattern[i] == '?')
        {
            bytes.push_back(0);
            mask += '?';
        }
        else if (pattren_len > i + 1 && std::isxdigit(pattern[i]) && std::isxdigit(pattern[i+1]))
        {
            bytes.push_back(std::stoi(pattern.substr(i++, 2), nullptr, 16));
            mask += 'x';
        }
    }

    if (bytes.empty() || mask.empty() || bytes.size() != mask.size())
        return list;

    list = findBytesAll(start, end, bytes.data(), mask);
    return list;
}

uintptr_t findIdaPatternFirst(const uintptr_t start, const uintptr_t end, const std::string& pattern)
{
    if (start >= end)
        return 0;

    std::string mask;
    std::vector<char> bytes;

    const size_t pattren_len = pattern.length();
    for (std::size_t i = 0; i < pattren_len; i++)
    {
        if (pattern[i] == ' ') continue;
		
        if (pattern[i] == '?')
        {
            bytes.push_back(0);
            mask += '?';
        }
        else if (pattren_len > i + 1 && std::isxdigit(pattern[i]) && std::isxdigit(pattern[i+1]))
        {
            bytes.push_back(std::stoi(pattern.substr(i++, 2), nullptr, 16));
            mask += 'x';
        }
    }

    if (bytes.empty() || mask.empty() || bytes.size() != mask.size()) {
        printf("uehshsh\n");
        return 0;
    }

    return findBytesFirst(start, end, bytes.data(), mask);
}

std::vector<uintptr_t> findDataAll(const uintptr_t start, const uintptr_t end, const void *data, size_t size)
{
    std::vector<uintptr_t> list;

    if (start >= end || !data || size < 1)
        return list;

    std::string mask(size, 'x');

    list = findBytesAll(start, end, (const char *)data, mask);
    return list;
}

uintptr_t findDataFirst(const uintptr_t start, const uintptr_t end, const void *data, size_t size)
{
    if (start >= end || !data || size < 1)
        return 0;

    std::string mask(size, 'x');

    return findBytesFirst(start, end, (const char *)data, mask);
}

}