// 大部分代码来自于 http://t.csdn.cn/bQCd5
// 我在其基础上对搜索函数进行了优化,并封装了一些常用函数
// 支持编译共享库和二进制
#ifndef MEMORY_TOOLS_H
#define MEMORY_TOOLS_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <fcntl.h>
#include <unordered_map>
#include <sys/mman.h>
#include <future>
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <queue>
#include <cstdlib>
#include <unordered_set>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

// 支持的搜索类型
enum
{
	DWORD,
	FLOAT,
	BYTE,
	WORD,
	QWORD,
	XOR,
	DOUBLE,
};

// 支持的内存范围(请参考GG修改器内存范围)
enum
{
	Mem_Auto,					// 所有内存页
	Mem_A,
	Mem_Ca,
	Mem_Cd,
	Mem_Cb,
	Mem_Jh,
	Mem_J,
	Mem_S,
	Mem_V,
	Mem_Xa,
	Mem_Xs,
	Mem_As,
	Mem_B,
	Mem_O,
};

struct MemPage
{
	long start;
	long end;
	char flags[8];
	char name[128];
	void *buf = NULL;
};

struct AddressData
{
	std::vector<long>addrs;
};

// 根据类型判断类型所占字节大小
size_t judgSize(int type)
{
	switch (type)
	{
	case DWORD:
	case FLOAT:
	case XOR:
		return 4;
	case BYTE:
		return sizeof(char);
	case WORD:
		return sizeof(short);
	case QWORD:
		return sizeof(long);
	case DOUBLE:
		return sizeof(double);
	}
	return 4;
}


class MemoryDebug
{
  private:
	pid_t pid = 0;				// 调试应用的PID
	std::vector<long>res;
    
    std::vector<long>ofstmp;
  public:
	// 设置调试的应用包名，返回PID
	int setPackageName(const char *name);
	// 获取模块的基址，@name：模块名，@index：模块在内存中的内存页位置(第几位，从1开始，默认1)
	long getModuleBase(const char *name, int index = 1);
	// 获取模块的BSS基址
	long getBssModuleBase(const char *name);
	// 读内存的基础函数
	size_t preadv(long address, void *buffer, size_t size);
	// 写内存的基础函数
	size_t pwritev(long address, void *buffer, size_t size);
    
    template < class T > void Editoffest(T num, int offset);
    template < class T > void searchoffest(T num, int offset);
    template < class T > void SearchOffest(T num, int offset);
	// 传lambda进去
    template < typename F > int searchCall(int type, F && call);
    template < class T > void EditXa(long address, T newValue);
	// 搜索字符串
    template < typename T > AddressData searchString(const std::string & str, int mem, bool debug = false);
	// 仿memorytools函数
    template < class T > void searchMem(T value, int type, int mem);
    template < class T > AddressData searchPointer(std::vector < long >address, long offset,
													 int type, int mem, bool debug = false);
	// 根据值搜索内存，并返回相应地址
    template < class T > AddressData search(T value, int type, int mem, bool debug = false);
	// 修改内存地址值，返回-1，修改失败，返回1，修改成功
    template < class T > int edit(T value, long address, int type, bool debug = false);

	// 多包名读取
	int getPidByPackageNames(std::vector < std::string > packageNames);
	// 读取一个DWORD(int)数值
	int ReadDword(long address);
	// 读取一个int指针地址数值
	long ReadDword64(long address);
	// 读取一个float类型数值
	float ReadFloat(long address);
	// 读取一个long类型数值
	long ReadLong(long address);
	// 32位指针跳转
	long int ReadPointer(long address);
    
    std::vector<long> getSearchResult();
};





/*


template < typename T >
	AddressData MemoryDebug::searchString(const std::string & str, int mem, bool debug)
{
	MemPage *mp = nullptr;
	AddressData ad;
	char path[128] = "";
	char *line = nullptr;
	size_t mapsize = 0;
	sprintf(path, "/proc/%d/maps", pid);
	auto handle = fopen(path, "r");

	std::vector < T > bytes;
  for (char c:str)
	{
		bytes.push_back(c);
	}


	while (getline(&line, &mapsize, handle) > 0)
	{
		mp = (MemPage *) calloc(1, sizeof(MemPage));
		sscanf(line, "%p-%p %s %*p %*p:%*p %*p %[^\n]%s", &mp->start, &mp->end, mp->flags,
			   mp->name);
		if ((memContrast(mp->name, mp->flags) == mem || mem == Mem_Auto)
			&& strstr(mp->flags, "r") != nullptr)
		{
			std::unique_ptr < T[] > buf(new T[mp->end - mp->start]);
			preadv(mp->start, buf.get(), mp->end - mp->start);
			for (size_t i = 0; i < mp->end - mp->start; ++i)
			{
				bool match = true;
				for (size_t j = 0; j < bytes.size(); ++j)
				{
					if (buf[i + j] != bytes[j])
					{
						match = false;
						break;
					}
				}
				if (match)
				{
					ad.addrs.push_back(mp->start + i);
				}
			}
			free(mp);
		}
	}
	fclose(handle);
	return ad;
}
*/

template < class T > int MemoryDebug::edit(T value, long address, int type, bool debug)
{
	if (-1 == pwritev(address, &value, judgSize(type)))
	{
		if (debug)
			printf("修改失败-> addr:%p\n", address);
		return -1;
	}
	else
	{
		if (debug)
			printf("修改成功-> addr:%p\n", address);
		return 1;
	}
	return -1;
}

template < class T > void MemoryDebug::EditXa(long address, T newValue)
{
	char lj[64];
	int handle;
	sprintf(lj, "/proc/%d/mem", pid);
	handle = open(lj, O_RDWR);
	lseek(handle, 0, SEEK_SET);
	pwrite64(handle, &newValue, sizeof(T), address);
	close(handle);
}

long MemoryDebug::ReadDword64(long address)
{
	long local_ptr = 0;
	preadv(address, &local_ptr, 8);
	return local_ptr;
}

long int MemoryDebug::ReadPointer(long address)
{
	long int local_value = 0;
	preadv(address, &local_value, 4);
	return local_value;
}

int MemoryDebug::ReadDword(long address)
{
	int local_value = 0;
	preadv(address, &local_value, sizeof(char));
	return local_value;
}




float MemoryDebug::ReadFloat(long address)
{
	float local_value = 0;
	preadv(address, &local_value, 4);
	return local_value;
}

long MemoryDebug::ReadLong(long address)
{
	long local_value = 0;
	preadv(address, &local_value, 8);
	return local_value;
}

void getRoot(char **argv)
{
	char shellml[64];
	sprintf(shellml, "su -c %s", *argv);
	if (getuid() != 0)
	{
		system(shellml);
		exit(1);
	}
}

#endif
