#pragma once
#include "common.h"
#include "central_cache.h"

class ThreadCache {
public:
	//大きさがbytesのメモリ領域を確保
	void* Allocate(size_t bytes);
	//ptrが指している大きさがbytesのメモリ領域を解放
	void Deallocate(void* ptr, size_t bytes);
private:
	//ThreadCacheに保有するメモリ領域が足りない場合、CentralCacheから確保
	void FetchFromCentralCache(size_t bytes_object);
	//ThreadCacheに保有するメモリ領域が特定の数を超える場合、CentralCacheにメモリ領域を解放
	void ReleaseToCentralCache(FreeList& free_list, size_t num_free, size_t bytes_object);
	//スレッド独占するメモリのキャッシュ
	FreeList _free_lists[kNumFreeList];
};
//TLS、スレッドごとにThreadCache一つ保有
_declspec (thread) static ThreadCache* p_thread_cache = nullptr;
