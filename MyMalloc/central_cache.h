#pragma once
#include "common.h"
#include "page_cache.h"

class CentralCache {
public:
	//シングルトン
	CentralCache(const CentralCache&) = delete;
	CentralCache(CentralCache&&) = delete;
	CentralCache& operator=(const CentralCache&) = delete;
	CentralCache& operator=(CentralCache&&) = delete;
	static CentralCache& GetInsatnce();

	//大きさがbytes_objectの領域をnum_object個取得するのを申し込み、実際にnum_acture個を取得
	size_t FetchRange(void*& start, void*& end, size_t num_object, size_t bytes_object);
	//メモリ領域のリストをそれが所属するSpanに返す
	void ReleaseListToSpans(void* start, void* end, size_t num_free, size_t bytes_object);
private:
	//シングルトン
	CentralCache() {};
	inline static std::unique_ptr<CentralCache> _p_instance;
	inline static std::mutex _mtx;

	//CentralCacheにメモリ領域が足りない場合、PageCacheからSpanを一つ取得し、そのFreeListを用意
	Span* FetchSpanFromPageCache(size_t bytes_object);
	//保有するメモリ領域が一つも利用されていない場合、SpanをPageCacheに返還
	void ReleaseSpanToPageCache(Span* p_span);

	SpanList _span_lists[kNumFreeList];
};

