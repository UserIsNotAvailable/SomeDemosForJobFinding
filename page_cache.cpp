#include "page_cache.h"

//シングルトン、PageCacheのInsatnceを取得
PageCache& PageCache::GetInsatnce() {
	if (nullptr != _p_instance)return *_p_instance;
	std::unique_lock<std::mutex> lck(_mtx, std::defer_lock);
	lck.lock();
	if (nullptr == _p_instance) {
		_p_instance.reset(new PageCache());
	}
	lck.unlock();
	return *_p_instance;
}

//マルチスレッド対応
Span* PageCache::NewSpan(PageId num_page) {
	std::unique_lock<std::mutex> lck(_mtx, std::defer_lock);
	lck.lock();
	Span* new_span = _NewSpan(num_page);
	lck.unlock();
	return new_span;
}


//未使用のメモリ領域の情報を保有するSpanを取得
Span* PageCache::_NewSpan(PageId num_page) {
	//_span_listsのindexがnum_pageのSpanListから取得
	if (!_span_lists[num_page].Empty()) {
		return _span_lists[num_page].PopFront();
	}

	//num_pageよりページ数が大きいSpanから取得
	for (PageId i = num_page; i < kMaxPage + 1; ++i) {
		if (!_span_lists[i].Empty()) {

			//p_originalの「頭」から、num_page個のページを切って、p_splitに入れる
			Span* p_original = _span_lists[i].PopFront();
			Span* p_split = new Span();//TODO
			p_split->setStartPageId(p_original->getStartPageId() + p_original->getTotalPageCount() - num_page);
			p_split->setTotalPageCount(num_page);

			//p_originalが保有するページ数が少なくなったため、別のSpanListに入れる
			p_original->setTotalPageCount(p_original->getTotalPageCount() - num_page);
			_span_lists[p_original->getTotalPageCount()].PushFront(p_original);

			//p_originalからp_splitに移ったページの情報を_id_span_mapに更新
			for (PageId id = 0; id < p_split->getTotalPageCount(); ++id) {
				_id_span_map[p_split->getStartPageId() + id] = p_split;
			}

			return p_split;
		}
	}

	//上記処理からSpanが取得できない場合、システムから128ページを纏めて取得し、128ページのメモリ領域を保有するSpanを新規作成
	void* ptr = SystemAllocPage(kMaxPage);
	Span* new_span = new Span();//TODO
	new_span->setStartPageId(reinterpret_cast<PageId>(ptr) >> kPageShift);
	new_span->setTotalPageCount(kMaxPage);

	//新しく取得した128ページのIDとnew_spanと紐づける
	for (PageId id = 0; id < new_span->getTotalPageCount(); ++id) {
		_id_span_map[new_span->getStartPageId() + id] = new_span;
	}

	//新規作成のSpanをPageCacheに保存
	_span_lists[new_span->getTotalPageCount()].PushFront(new_span);

	//ここまで来るとはもともとPageCacheに使えるSpanは存在しなかったことを意味する
	//そのため_NewSpanをもう一度呼び出し、上記取得した128ページのSpanを「頭」からnum_pageのページを切って、
	//NewSpanを作って、呼び出し元に返す
	return _NewSpan(num_page);
}

//SpanをPageCacheに返し、
//それに保有する最小のページの前のページも他のSpanに管理され、しかもそのSpanが未使用の場合、二つのSpanをMerge
//それに保有する最大のページの後のページも他のSpanに管理され、しかもそのSpanが未使用の場合、二つのSpanをMerge
void PageCache::FreeSpan(Span* p_span) {

	//前へMerge
	while (true) {
		//p_spanに保有する最小のページの前のページのIDを計算
		PageId id_prev = p_span->getStartPageId() - 1;

		//前のページのIDが_id_span_mapに存在しない、つまりPageCacheに管理されていない場合、前へMergeを中止
		auto itr = _id_span_map.find(id_prev);
		if (itr == _id_span_map.end()) {
			break;
		}

		//前ののSpanが存在し、それが利用中もしくは合併したら128ページ超え、PageCacheが格納できない場合、前へMergeを中止
		Span* p_span_prev = itr->second;
		if (!p_span_prev->Full() || p_span->getTotalPageCount() + p_span_prev->getTotalPageCount() > kMaxPage) {
			break;
		}

		//p_span_prevをp_spanにMerge
		_span_lists[p_span_prev->getTotalPageCount()].Erase(p_span_prev);
		p_span->setStartPageId(p_span_prev->getStartPageId());
		p_span->setTotalPageCount(p_span_prev->getTotalPageCount() + p_span->getTotalPageCount());

		//Merge後_id_span_mapを更新
		for (PageId id = 0; id < p_span_prev->getTotalPageCount(); ++id) {
			_id_span_map[p_span_prev->getStartPageId() + id] = p_span;
		}
		delete p_span_prev;//TODO
	}

	//後ろへMerge
	while (true) {
		PageId id_next = p_span->getStartPageId() + p_span->getTotalPageCount();
		auto itr = _id_span_map.find(id_next);
		if (itr == _id_span_map.end()) {
			break;
		}
		Span* p_span_next = itr->second;
		if (!p_span_next->Full() || p_span->getTotalPageCount() + p_span_next->getTotalPageCount() > kMaxPage) {
			break;
		}
		_span_lists[p_span_next->getTotalPageCount()].Erase(p_span_next);

		p_span->setTotalPageCount(p_span_next->getTotalPageCount() + p_span->getTotalPageCount());

		for (PageId id = 0; id < p_span_next->getTotalPageCount(); ++id) {
			_id_span_map[p_span_next->getStartPageId() + id] = p_span;
		}
		delete p_span_next;//TODO
	}
	_span_lists[p_span->getTotalPageCount()].PushFront(p_span);
}

//ページIDからそのページを保有するSpanを取得
Span* PageCache::GetSpanRefFromPageId(PageId id) {
	auto itr = _id_span_map.find(id);
	if (itr != _id_span_map.end()) {
		return itr->second;
	}
	return nullptr;
}


//システムからnum_page個のページを確保
void* PageCache::SystemAllocPage(PageId num_page) {
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, num_page * (1 << kPageShift),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// brk mmapなど
#endif
	if (ptr == nullptr) throw std::bad_alloc();
	return ptr;
}

//システムにページを解放
void PageCache::SystemFreePage(void* ptr) {
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// brk mmapなど
#endif
}
