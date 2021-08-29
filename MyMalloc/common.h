#pragma once
#include<cassert>
#include<unordered_map>
#include <mutex>

#ifdef _WIN32
#include<Windows.h>
#else
//brk、mmapなど
#endif

//ThreadCacheが扱うバイト数の最大値、16ページ(1ページ==4kb)
const size_t kMaxBytes = 1024 * 4 * 16;

//ThreadCacheが保有するFreeListの数
//CentralCacheが保有するSpanListの数
const size_t kNumFreeList = 16 + 56 + 56 + 112;

//PageCacheが保有するSpanListの数、最大128ページ
const size_t kMaxPage = 128;

//ページIDとメモリ領域へのポインタとの変換用シフト値
const size_t kPageShift = 12;

//FreeListのノードに保存する次のノードを取得
inline void*& NextObject(void* obj) {
	return *(static_cast<void**>(obj));
}

//メモリ領域を片方向リストで管理するクラス
class FreeList {
public:
	bool Empty() {
		return _free_list == nullptr;
	}

	size_t Size() {
		return _num_object;
	}

	void Push(void* obj) {
		NextObject(obj) = _free_list;
		_free_list = obj;
		++_num_object;
	}

	//区切ったメモリ領域を纏めてリストに挿入、numは領域の数を表す
	void PushRange(void* start, void* end, size_t num) {
		NextObject(end) = _free_list;
		_free_list = start;
		_num_object += num;
	}

	void* Pop() {
		assert(_free_list);
		void* ret = _free_list;
		_free_list = NextObject(_free_list);
		--_num_object;
		return ret;
	}
	//FreeListからnum_object個の領域を取得する
	//実際に取得した領域の数num_actureを戻り値として返す
	//FreeListが保有する領域の数が足りない場合num_acture < num_object
	size_t PopRange(void*& start, void*& end, size_t num_object) {
		size_t num_acture = 0;
		void* prev = nullptr;
		void* cur = _free_list;
		while (nullptr != cur && num_acture < num_object) {
			prev = cur;
			cur = NextObject(cur);
			++num_acture;
		}
		start = _free_list;
		end = prev;
		NextObject(end) = nullptr;
		_free_list = cur;
		_num_object -= num_acture;
		return num_acture;
	}

	void Clear() {
		_free_list = nullptr;
		_num_object = 0;
	}
private:
	//FreeListが管理するメモリ領域のリストの頭に指すポインタ
	void* _free_list = nullptr;
	//FreeListが管理するメモリ領域の数
	size_t _num_object = 0;
};
#ifdef _WIN32
typedef unsigned int PageId;
#else 
typedef unsigned long long PageId;
#endif// _WIN32

//バイト数を切り上げる関数、バイト数からFreeListの配列のindexを計算する関数
//などのUtilを保有するクラス
class SizeClass {
public:
	//bytesを切り上げる
	static inline size_t RoundUp(size_t bytes) {
		assert(bytes <= kMaxBytes);
		//bytes∈[1,128]：8byteごとに切り上げ、8、16、24...128、freelist[0]～freelist[15]に対応、計16個
		if (bytes <= 128) {
			return RoundUp(bytes, 8);
		}
		//bytes∈[129,1024]：16byteごとに切り上げ、144、160、172...1024、freelist[16]～freelist[71]に対応、計56個
		else if (bytes <= 1024) {
			return RoundUp(bytes, 16);
		}
		//bytes∈[1025,8*1024]：128byteごとに切り上げ、1152、1280、1408...8*1024、freelist[72]～freelist[127]に対応、計56個
		else if (bytes <= 8192) {
			return RoundUp(bytes, 128);
		}
		//bytes∈[8*1024+1,64*1024]：512byteごとに切り上げ、8*1024+512、8*1024+1024、8*1024+1536...64*1024、freelist[128]～freelist[239]に対応、計112個
		else if (bytes <= 65536) {
			return RoundUp(bytes, 512);
		}
		return -1;
	}

	//bytesをalignごとに区切って切り上げる
	static inline size_t RoundUp(size_t bytes, size_t align) {
		return (((bytes)+align - 1) & ~(align - 1));
	}

	//bytesからFreeListの配列のindexを算出
	static inline size_t Index(size_t bytes) {
		assert(bytes <= kMaxBytes);
		static int group_array[4] = { 16, 56, 56, 112 };
		if (bytes <= 128) {
			return Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8192) {
			return Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 65536) {
			return Index(bytes - 8192, 9) + group_array[2] + group_array[1] + group_array[0];
		}
		return -1;
	}

	//bytesとalign_shiftよりFreeListの配列のindexを計算
	static inline size_t Index(size_t bytes, size_t align_shift) {
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	//bytes_objectよりCentralCacheから取得するメモリ領域の数を算出
	static size_t NumFetchObject(size_t bytes_object) {
		if (bytes_object == 0) return 0;
		int num = static_cast<int>(kMaxBytes / bytes_object);
		if (num < 2)num = 2;
		if (num > 512)num = 512;
		return num;
	}

	//bytes_objectよりPageCacheから取得するページ数を算出
	static PageId NumFetchPage(size_t bytes_object) {
		size_t num_object = NumFetchObject(bytes_object);
		PageId num_page = (num_object * bytes_object) >> kPageShift;
		if (num_page == 0)	num_page = 1;
		return num_page;
	}

};

//CentralCache、PageCacheにおいて、それが確保するメモリ領域を管理するクラス
//SpanListにて双方向、循環リストとの構造で管理
class Span {
	friend class SpanListIterator;
	friend class SpanList;
	Span* prev = nullptr;
	Span* next = nullptr;
public:
	//新規作成したSpanにメモリ領域を一つ追加
	void AddObject(void* obj) {
		_free_list.Push(obj);
	}

	//Spanにメモリ領域を返還
	void RestoreObject(void* obj) {
		_free_list.Push(obj);
		setUsedObjectCount(getUsedObjectCount() - 1);
	}

	//Spanが保有するFreeListからnum_object個の領域を取得する
	//実際に取得した領域の数num_actureを戻り値として返す
	size_t FetchRange(void*& start, void*& end, size_t num_object) {
		size_t num_acture = _free_list.PopRange(start, end, num_object);
		setUsedObjectCount(getUsedObjectCount() + num_acture);
		return num_acture;
	}

	void Clear() {
		_free_list.Clear();
		setObjectSize(0);
		setUsedObjectCount(0);
	}

	bool Empty() {
		return _free_list.Empty();
	}

	bool Full() {
		return !_free_list.Empty() && 0 == getUsedObjectCount();
	}

	PageId getStartPageId() {
		return start_page_id;
	}

	void setStartPageId(PageId new_id) {
		start_page_id = new_id;
	}

	size_t getTotalPageCount() {
		return total_page_count;
	}

	void setTotalPageCount(size_t new_count) {
		total_page_count = new_count;
	}

	size_t getUsedObjectCount() {
		return used_object_count;
	}

	void setUsedObjectCount(size_t new_count) {
		used_object_count = new_count;
	}

	size_t getObjectSize() {
		return object_size;
	}

	void setObjectSize(size_t new_size) {
		object_size = new_size;
	}
private:
	//Spanが保有するメモリ領域の一番小さいページID
	PageId start_page_id = 0;
	//Spanが保有するメモリ領域のページ数
	size_t total_page_count = 0;
	//該当Spanにおいてユーザ利用中の領域の数
	size_t used_object_count = 0;
	//該当Spanが保有するメモリ領域一つ当たりの大きさ
	size_t object_size = 0;
	//メモリを管理するFreeList
	FreeList _free_list;
};

//SpanListのイテレータ
class SpanListIterator {
	typedef Span* PtrSpan;
	typedef Span& RefSpan;
	typedef SpanListIterator Self;
public:
	SpanListIterator(PtrSpan ptr_node = nullptr)
		:_ptr_node(ptr_node) {
	}

	SpanListIterator(const Self& another)
		:_ptr_node(another._ptr_node) {
	}

	RefSpan operator*() {
		return *_ptr_node;
	}

	const RefSpan operator*()const {
		return *_ptr_node;
	}

	PtrSpan operator&() {
		return _ptr_node;
	}

	const PtrSpan operator&()const {
		return _ptr_node;
	}

	PtrSpan operator->() {
		return _ptr_node;
	}

	const PtrSpan operator->()const {
		return _ptr_node;
	}

	Self& operator++() {
		_ptr_node = _ptr_node->next;
		return *this;
	}

	Self operator++(int) {
		Self tmp(*this);
		_ptr_node = _ptr_node->next;
		return tmp;
	}

	Self& operator--() {
		_ptr_node = _ptr_node->prev;
		return *this;
	}

	Self operator--(int) {
		Self tmp(*this);
		_ptr_node = _ptr_node->prev;
		return tmp;
	}

	bool operator==(const Self& another) {
		return _ptr_node == another._ptr_node;
	}

	bool operator==(const Self& another)const {
		return _ptr_node == another._ptr_node;
	}

	bool operator!=(const Self& another) {
		return !(*this == another);
	}

	bool operator!=(const Self& another)const {
		return !(*this == another);
	}

private:
	PtrSpan _ptr_node;
};

//CentralCache、PageCacheにおいて、Spanを管理するクラス
//双方向、循環リスト
class SpanList {
public:
	SpanList() {
		_head = new Span();//TODO
		_head->next = _head;
		_head->prev = _head;
	}

	bool Empty() {
		return _head->next == _head;
	}

	void Insert(Span* pos, Span* new_span) {
		Span* prev = pos->prev;

		prev->next = new_span;
		new_span->prev = prev;

		new_span->next = pos;
		pos->prev = new_span;
	}

	void Erase(Span* pos) {
		assert(pos != _head);
		Span* prev = pos->prev;
		Span* next = pos->next;

		prev->next = next;
		next->prev = prev;

		pos->prev = nullptr;
		pos->next = nullptr;
	}

	void PushFront(Span* new_span) {
		Insert(_head->next, new_span);
	}

	void PushBack(Span* new_span) {
		Insert(_head, new_span);
	}

	Span* PopFront() {
		assert(!Empty());
		Span* tmp = _head->next;
		Erase(_head->next);
		return tmp;
	}

	Span* PopBack() {
		assert(!Empty());
		Span* tmp = _head->prev;
		Erase(_head->prev);
		return tmp;
	}

	SpanListIterator Begin() {
		return SpanListIterator(_head->next);
	}

	SpanListIterator End() {
		return SpanListIterator(_head);
	}

	void Lock() {
		_mtx.lock();
	}

	void UnLock() {
		_mtx.unlock();
	}
private:
	//Dmmy head
	Span* _head;
	//マルチスレッド対策
	std::mutex _mtx;
};
