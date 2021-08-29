#include "central_cache.h"
#include <iostream>

//シングルトン、CentralCacheのInsatnceを取得
CentralCache& CentralCache::GetInsatnce() {
	if (nullptr != _p_instance)return *_p_instance;
	std::unique_lock<std::mutex> lck(_mtx, std::defer_lock);
	lck.lock();
	if (nullptr == _p_instance) {
		_p_instance.reset(new CentralCache());
	}
	lck.unlock();
	return *_p_instance;
}

//大きさがbytes_objectの領域をnum_object個取得するのを申し込み、実際にnum_acture個を取得
size_t CentralCache::FetchRange(void*& start, void*& end, size_t num_object, size_t bytes_object) {
	Span* p_span = nullptr;
	size_t index = SizeClass::Index(bytes_object);
	SpanList& span_list = _span_lists[index];

	//マルチスレッド対応
	span_list.Lock();

	//span_listを走査し、未使用のメモリ領域を保有するSpanを取得
	auto itr = span_list.Begin();
	while (span_list.End() != itr) {
		if (!itr->Empty()) {
			p_span = &itr;
			break;
		}
		else {
			++itr;
		}
	}

	//span_listにあるすべてのSpanが余剰メモリがない場合、PageCacheからSpanを一つ取得
	if (span_list.End() == itr) {
		p_span = FetchSpanFromPageCache(bytes_object);
		span_list.PushFront(p_span);
	}
	size_t num_acture = p_span->FetchRange(start, end, num_object);

	//p_spanからメモリ取得後p_spanが空になった場合、それをspan_listの最後に置き、チューニング
	if (p_span->Empty()) {
		span_list.Erase(p_span);
		span_list.PushBack(p_span);
	}

	//マルチスレッド対応
	span_list.UnLock();

	return num_acture;
}

//CentralCacheにメモリ領域が足りない場合、PageCacheからSpanを一つ取得し、そのFreeListを用意
Span* CentralCache::FetchSpanFromPageCache(size_t bytes_object) {

	//PageCacheからSpanを一つ取得
	//この段階ではp_spanにページに関する情報のみ保有する
	PageId num_page = SizeClass::NumFetchPage(bytes_object);
	Span* p_span = PageCache::GetInsatnce().NewSpan(num_page);

	//p_spanが保有するページのID、ページ数から、そのメモリ領域へのポインタを計算
	char* start = (char*)(p_span->getStartPageId() << kPageShift);
	char* end = start + (p_span->getTotalPageCount() << kPageShift);
	//bytes_object区切りでメモリを小さい領域に切って、一個ずつp_spanのFreeListに入れる
	while (start < end) {
		char* obj = start;
		start += bytes_object;
		p_span->AddObject(obj);
	}
	p_span->setObjectSize(bytes_object);

	return p_span;
}

//メモリ領域のリストをそれが所属するSpanに返す
void CentralCache::ReleaseListToSpans(void* start, void* end, size_t num_free, size_t bytes_object) {
	size_t index = SizeClass::Index(bytes_object);
	SpanList& span_list = _span_lists[index];
	//マルチスレッド対応
	span_list.Lock();

	//ThreadCacheから返還したメモリ領域リストの領域を一つずつそれが所属するSpanに戻す
	while (start) {
		void* next = NextObject(start);
		//メモリ領域が所属するページのIDを取得
		PageId id = reinterpret_cast<PageId> (start) >> kPageShift;
		//ページのIDからそのページが所属するSpanを取得し、メモリ領域を返還
		Span* p_span = PageCache::GetInsatnce().GetSpanRefFromPageId(id);
		if (p_span) {
			p_span->RestoreObject(start);
		}

		//上記取得したp_spanが保有するメモリ領域は一つでも利用されていない場合、p_spanをPageCacheに返す
		if (p_span->Full()) {
			ReleaseSpanToPageCache(p_span);
		}

		start = next;
	}

	//マルチスレッド対応
	span_list.UnLock();
}

//保有するメモリ領域が一つも利用されていない場合、SpanをPageCacheに返還
void CentralCache::ReleaseSpanToPageCache(Span* p_span) {
	//CentralCacheのSpanListからp_spanを削除
	size_t index = SizeClass::Index(p_span->getObjectSize());
	_span_lists[index].Erase(p_span);

	//p_spanのFreeListをクリアし、メモリ領域に関する情報を削除
	//ページに関する情報のみそのまま保持する
	p_span->Clear();

	PageCache::GetInsatnce().FreeSpan(p_span);
}
