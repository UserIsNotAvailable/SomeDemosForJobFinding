#pragma once
#include "common.h"

class PageCache {
public:
	//シングルトン
	PageCache(const PageCache&) = delete;
	PageCache(PageCache&&) = delete;
	PageCache& operator=(const PageCache&) = delete;
	PageCache& operator=(PageCache&&) = delete;
	static PageCache& GetInsatnce();

	//未使用のメモリ領域の情報を保有するSpanを取得
	Span* NewSpan(PageId num_page);
	//SpanをPageCacheに返す
	void FreeSpan(Span* p_span);
	//ページIDからそのページを保有するSpanを取得
	Span* GetSpanRefFromPageId(PageId id);

	//システムからnum_page個のページを確保
	void* SystemAllocPage(PageId num_page);
	//システムにページを解放
	void SystemFreePage(void* ptr);


private:
	//シングルトン
	PageCache() {};
	inline static std::unique_ptr<PageCache> _p_instance;
	inline static std::mutex _mtx;

	Span* _NewSpan(PageId num_page);

	//ページIDとそのページが所属するSpanのMap
	std::unordered_map<PageId, Span*> _id_span_map;

	SpanList _span_lists[kMaxPage + 1];
};
