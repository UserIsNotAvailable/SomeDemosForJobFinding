#pragma once
#include "thread_cache.h"

//bytesサイズ分のメモリ領域を確保
static void* MyMalloc(size_t bytes) {
	//スレッドごとにThreadCacheを保持する
	if (nullptr == p_thread_cache) {
		p_thread_cache = new ThreadCache();//TODO
	}
	//[1b,16*4kb] ThreadCacheより確保
	if (bytes <= kMaxBytes) {
		return p_thread_cache->Allocate(bytes);
	}
	//(16*4kb,128*4kb] PageCacheより確保
	else if (bytes <= (kMaxPage << kPageShift)) {
		size_t bytes_aligned = SizeClass::RoundUp(bytes, 1 << kPageShift);
		PageId num_page = (bytes_aligned >> kPageShift);
		Span* p_span = PageCache::GetInsatnce().NewSpan(num_page);
		p_span->setObjectSize(bytes_aligned);
		p_span->setUsedObjectCount(1);
		void* ptr = reinterpret_cast<void*>(p_span->getStartPageId() << kPageShift);
		return ptr;
	}
	//(128*4kb,+∞] システムのインタフェースより確保
	else {
		size_t bytes_aligned = SizeClass::RoundUp(bytes, 1 << kPageShift);
		PageId num_page = (bytes_aligned >> kPageShift);
		return PageCache::GetInsatnce().SystemAllocPage(num_page);
	}
}
//ptrが指しているメモリ領域を解放
static void MyFree(void* ptr) {
	//ptrより、確保されているメモリが所属するページのIDを取得
	PageId id = reinterpret_cast<PageId>(ptr) >> kPageShift;
	Span* p_span = PageCache::GetInsatnce().GetSpanRefFromPageId(id);
	if (p_span) {
		size_t bytes_object = p_span->getObjectSize();
		//[1b,16*4kb] ThreadCacheより解放
		if (bytes_object <= kMaxBytes) {
			p_thread_cache->Deallocate(ptr, bytes_object);
		}
		//(16*4kb,128*4kb] PageCacheより解放
		else if (bytes_object <= (kMaxPage << kPageShift)) {
			p_span->Clear();
			PageCache::GetInsatnce().FreeSpan(p_span);
		}
	}
	//(128*4kb,+∞] システムのインタフェースより解放
	else {
		PageCache::GetInsatnce().SystemFreePage(ptr);
	}
}

