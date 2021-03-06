#include "patch_analyzer.h"
#include "../utils/debug.h"
//---
using namespace pesieve;

template <typename DELTA_T>
ULONGLONG pesieve::PatchAnalyzer::getJmpDestAddr(ULONGLONG currVA, int instrLen, DELTA_T lVal)
{
	int delta = instrLen + int(lVal);
	ULONGLONG addr = currVA + delta;
	return addr;
}

size_t pesieve::PatchAnalyzer::parseShortJmp(PatchList::Patch &patch, PBYTE patch_ptr, ULONGLONG patch_va)
{
	const size_t instr_size = 2;

	BYTE *lval = (BYTE*)((ULONGLONG)patch_ptr + 1);
	ULONGLONG addr = getJmpDestAddr<BYTE>(patch_va, instr_size, (*lval));

	patch.setHookTarget(addr);
	return instr_size;
}

size_t pesieve::PatchAnalyzer::parseJmp(PatchList::Patch &patch, PBYTE patch_ptr, ULONGLONG patch_va)
{
	const size_t instr_size = 5;

	DWORD *lval = (DWORD*)((ULONGLONG) patch_ptr + 1);
	ULONGLONG addr = getJmpDestAddr<DWORD>(patch_va, instr_size, (*lval));

	patch.setHookTarget(addr);
	return instr_size;
}

size_t pesieve::PatchAnalyzer::parseMovJmp(PatchList::Patch &patch, PBYTE patch_ptr, bool is_long)
{
	size_t mov_instr_len = is_long ? 9 : 5;
	PBYTE jmp_ptr = patch_ptr + mov_instr_len; // next instruction

	if (is64Modifier(*patch_ptr)) {
		patch_ptr++;
		jmp_ptr++;
		mov_instr_len++; // add length of modifier
	}
	
	DWORD reg_id0 = patch_ptr[0] - 0xB8;

	// before call/jmp there can be also the modifier...
	if (is64Modifier(*jmp_ptr)) {
		jmp_ptr++;
		mov_instr_len++; // add length of modifier
	}
	DWORD reg_id1 = 0;
	if (jmp_ptr[0] == 0xFF && jmp_ptr[1] >= 0xE0 && jmp_ptr[1] <= 0xEF ) { // jmp reg
		//jmp reg
		reg_id1 = jmp_ptr[1] - 0xE0;
	} else if (jmp_ptr[0] == 0xFF && jmp_ptr[1] >= 0xD0 && jmp_ptr[1] <= 0xDF ) { // call reg
		//jmp reg
		reg_id1 = jmp_ptr[1] - 0xD0;
	} else {
#ifdef _DEBUG
		std::cerr << "It is not MOV->JMP" << std::hex << (DWORD)jmp_ptr[0] << std::endl;
#endif
		return NULL;
	}
	//TODO: take into account also modifiers
	if (reg_id1 != reg_id0) {
#ifdef _DEBUG
		std::cerr << "MOV->JMP : reg mismatch" << std::endl;
#endif
		return NULL;
	}
	size_t patch_size = mov_instr_len;
	ULONGLONG addr = NULL;
	if (!is_long) { //32bit
		DWORD *lval = (DWORD*)((ULONGLONG) patch_ptr + 1);
		addr = *lval;
	} else { //64bit
		ULONGLONG *lval = (ULONGLONG*)((ULONGLONG) patch_ptr + 1);
		addr = *lval;
	}
	patch_size += 2; //add jump reg size
	patch.setHookTarget(addr);
	DEBUG_PRINT("----> Target: " << std::hex << addr << std::endl);
	return patch_size;
}

size_t pesieve::PatchAnalyzer::parsePushRet(PatchList::Patch &patch, PBYTE patch_ptr)
{
	size_t instr_size = 5;
	PBYTE ret_ptr = patch_ptr + instr_size; // next instruction
	if (ret_ptr[0] != 0xC3) {
		return NULL; // this is not push->ret
	}
	instr_size++;
	DWORD *lval = (DWORD*)((ULONGLONG) patch_ptr + 1);
	patch.setHookTarget(*lval);
	return instr_size;
}

bool pesieve::PatchAnalyzer::is64Modifier(BYTE op)
{
	if (!isModule64bit) return false;
	if (op >= 0x40 && op <= 0x4F) { // modifier
		return true;
	}
	return false;
}

bool pesieve::PatchAnalyzer::isLongModifier(BYTE op)
{
	if (!isModule64bit) return false;
	if (op >= 0x48 && op <= 0x4F) { // modifier
		return true;
	}
	return false;
}

size_t pesieve::PatchAnalyzer::analyze(PatchList::Patch &patch)
{
	ULONGLONG patch_va = moduleData.rvaToVa(patch.startRva);
	size_t patch_offset = patch.startRva - sectionRVA;
	PBYTE patch_ptr = this->patchedCode + patch_offset;

	BYTE op = patch_ptr[0];
	if (op == OP_JMP || op == OP_CALL_DWORD) {
		return parseJmp(patch, patch_ptr, patch_va);
	}
	if (op == OP_SHORTJMP) {
		return parseShortJmp(patch, patch_ptr, patch_va);
	}
	if (op == OP_PUSH_DWORD) {
		return parsePushRet(patch, patch_ptr);
	}
	bool is_long = false;
	if (is64Modifier(op)) { // mov modifier
		if (isLongModifier(op)) {
			is_long = true;
		}
		op = patch_ptr[1];
	}
	if (op >= 0xB8 && op <= 0xBF) { // is mov
		return parseMovJmp(patch, patch_ptr, is_long);
	}
	return 0;
}

