#include "thread_cache.h"

//大きさがbytesのメモリ領域を確保
void* ThreadCache::Allocate(size_t bytes) {
	size_t index = SizeClass::Index(bytes);
	FreeList& free_list = _free_lists[index];

	//ThreadCacheに保有するメモリ領域が足りない場合、CentralCacheから確保
	if (free_list.Empty()) {
		size_t bytes_aligned = SizeClass::RoundUp(bytes);
		FetchFromCentralCache(bytes_aligned);
	}

	return free_list.Pop();
}

//ptrが指している大きさがbytesのメモリ領域を解放
void ThreadCache::Deallocate(void* ptr, size_t bytes) {
	size_t index = SizeClass::Index(bytes);
	FreeList& free_list = _free_lists[index];
	free_list.Push(ptr);

	//ThreadCacheに保有するメモリ領域が特定の数を超える場合、CentralCacheにメモリ領域を解放
	size_t bytes_aligned = SizeClass::RoundUp(bytes);
	size_t num_free = SizeClass::NumFetchObject(bytes_aligned);
	if (free_list.Size() >= num_free) {
		ReleaseToCentralCache(free_list, num_free, bytes_aligned);
	}
}
//ThreadCacheに保有するメモリ領域が足りない場合、CentralCacheから確保
void ThreadCache::FetchFromCentralCache(size_t bytes_object) {
	size_t index = SizeClass::Index(bytes_object);
	size_t num_object = SizeClass::NumFetchObject(bytes_object);
	void* start = nullptr, * end = nullptr;

	//CentralCacheから大きさがbytes_objectの領域をnum_object個取得するのを申し込み、実際にnum_acture個を取得
	size_t num_acture = CentralCache::GetInsatnce().FetchRange(start, end, num_object, bytes_object);
	_free_lists[index].PushRange(start, end, num_acture);
}
//ThreadCacheに保有するメモリ領域が特定の数を超える場合、CentralCacheにメモリ領域を解放
void ThreadCache::ReleaseToCentralCache(FreeList& free_list, size_t num_free, size_t bytes_object) {
	void* start = nullptr, * end = nullptr;
	free_list.PopRange(start, end, num_free);
	CentralCache::GetInsatnce().ReleaseListToSpans(start, end, num_free, bytes_object);
}

