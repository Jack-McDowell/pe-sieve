#include "mempage_data.h"
#include "module_data.h"
#include "../utils/debug.h"

using namespace pesieve;

bool pesieve::MemPageData::fillInfo()
{
	MEMORY_BASIC_INFORMATION page_info = { 0 };
	SIZE_T out = VirtualQueryEx(this->processHandle, (LPCVOID) start_va, &page_info, sizeof(page_info));
	if (out != sizeof(page_info)) {
		if (GetLastError() == ERROR_INVALID_PARAMETER) {
			return false;
		}
		DEBUG_PRINT("Could not query page: " << std::hex << start_va << ". Error: " << GetLastError() << std::endl);
		return false;
	}
	initial_protect = page_info.AllocationProtect;
	mapping_type = page_info.Type;
	protection = page_info.Protect;
	alloc_base = (ULONGLONG) page_info.AllocationBase;
	region_start = (ULONGLONG) page_info.BaseAddress;
	region_end = region_start + page_info.RegionSize;
	is_info_filled = true;
	return true;
}

bool pesieve::MemPageData::loadModuleName()
{
	const HMODULE mod_base = (HMODULE)this->alloc_base;
	std::string module_name = RemoteModuleData::getModuleName(processHandle, mod_base);
	if (module_name.length() == 0) {
		DEBUG_PRINT("Could not retrieve module name" << std::endl);
		return false;
	}
	this->module_name = module_name;
	return true;
}

bool pesieve::MemPageData::loadMappedName()
{
	if (!isInfoFilled() && !fillInfo()) {
		return false;
	}
	std::string mapped_filename = RemoteModuleData::getMappedName(this->processHandle, (HMODULE)this->alloc_base);
	if (mapped_filename.length() == 0) {
		DEBUG_PRINT("Could not retrieve name" << std::endl);
		return false;
	}
	this->mapped_name = mapped_filename;
	return true;
}

bool pesieve::MemPageData::isRealMapping()
{
	if (this->loadedData == nullptr && !fillInfo()) {
		DEBUG_PRINT("Not loaded!" << std::endl);
		return false;
	}
	if (!loadMappedName()) {
		DEBUG_PRINT("Could not retrieve name" << std::endl);
		return false;
	}
	HANDLE file = CreateFileA(this->mapped_name.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if(file == INVALID_HANDLE_VALUE) {
		DEBUG_PRINT("Could not open file!" << std::endl)
		return false;
	}
	HANDLE mapping = CreateFileMapping(file, 0, PAGE_READONLY, 0, 0, 0);
	if (!mapping) {
		DEBUG_PRINT("Could not create mapping!" << std::endl)
		CloseHandle(file);
		return false;
	}
	BYTE *rawData = (BYTE*) MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
	if (rawData == nullptr) {
		DEBUG_PRINT("Could not map view of file" << std::endl);
		CloseHandle(mapping);
		CloseHandle(file);
		return false;
	}

	bool is_same = false;
	size_t r_size = GetFileSize(file, 0);
	size_t smaller_size = this->loadedSize > r_size ? r_size : this->loadedSize;
	if (memcmp(this->loadedData, rawData, smaller_size) == 0) {
		is_same = true;
	}
	UnmapViewOfFile(rawData);
	CloseHandle(mapping);
	CloseHandle(file);
	return is_same;
}

bool pesieve::MemPageData::_loadRemote()
{
	_freeRemote();
	size_t region_size = size_t(this->region_end - this->start_va);
	if (stop_va && ( stop_va >= start_va  && stop_va < this->region_end)) {
		region_size = size_t(this->stop_va - this->start_va);
	}
	
	if (region_size == 0) {
		return false;
	}
	loadedData = peconv::alloc_aligned(region_size, PAGE_READWRITE);
	if (loadedData == nullptr) {
		return false;
	}
	this->loadedSize = region_size;
	bool is_guarded = (protection & PAGE_GUARD) != 0;

	size_t size_read = peconv::read_remote_memory(this->processHandle, (BYTE*)this->start_va, loadedData, loadedSize);
	if ((size_read == 0) && is_guarded) {
		DEBUG_PRINT("Warning: guarded page, trying to read again..." << std::endl);
		size_read = peconv::read_remote_memory(this->processHandle, (BYTE*)this->start_va, loadedData, loadedSize);
	}
	if (size_read == 0) {
		_freeRemote();
		DEBUG_PRINT("Cannot read remote memory!" << std::endl);
		return false;
	}
	return true;
}

