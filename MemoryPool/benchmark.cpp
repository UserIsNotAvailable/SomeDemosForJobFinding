#include "my_malloc.h"
#include<iostream>
#include<vector>
#include<cstdio>

void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds) {
	std::vector<std::thread> vthread(nworks);
	size_t malloc_costtime = 0;
	size_t free_costtime = 0;
	for (size_t k = 0; k < nworks; ++k) {
		vthread[k] = std::thread([&, k]() {
			std::vector<void*> v;
			v.reserve(ntimes);
			for (size_t j = 0; j < rounds; ++j) {
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++) {
					v.push_back(malloc(16));
				}
				size_t end1 = clock();
				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++) {
					free(v[i]);
				}
				size_t end2 = clock();
				v.clear();
				malloc_costtime += end1 - begin1;
				free_costtime += end2 - begin2;
			}
			});
	}
	for (auto& t : vthread) {
		t.join();
	}
	printf("%u threads run concurrently, each thread runs %u rounds, call malloc for %u times per round, costs %u ms\n",
		nworks, rounds, ntimes, malloc_costtime);
	printf("%u threads run concurrently, each thread runs %u rounds, call free for %u times per round, costs %u ms\n",
		nworks, rounds, ntimes, free_costtime);
	printf("%u threads run concurrently, call malloc and free for %u times, costs %u ms\n",
		nworks, nworks * rounds * ntimes, malloc_costtime + free_costtime);
}
void BenchmarkMyMalloc(size_t ntimes, size_t nworks, size_t rounds) {
	std::vector<std::thread> vthread(nworks);
	size_t malloc_costtime = 0;
	size_t free_costtime = 0;
	for (size_t k = 0; k < nworks; ++k) {
		vthread[k] = std::thread([&]() {
			std::vector<void*> v;
			v.reserve(ntimes);
			for (size_t j = 0; j < rounds; ++j) {
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++) {
					v.push_back(MyMalloc(16));
				}
				size_t end1 = clock();
				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++) {
					MyFree(v[i]);
				}
				size_t end2 = clock();
				v.clear();
				malloc_costtime += end1 - begin1;
				free_costtime += end2 - begin2;
			}
			});
	}
	for (auto& t : vthread) {
		t.join();
	}
	printf("%u threads run concurrently, each thread runs %u rounds, call MyMalloc for %u times per round, costs %u ms\n",
		nworks, rounds, ntimes, malloc_costtime);
	printf("%u threads run concurrently, each thread runs %u rounds, call MyFree for %u times per round, costs %u ms\n",
		nworks, rounds, ntimes, free_costtime);
	printf("%u threads run concurrently, call MyMalloc and MyFree for %u times, costs %u ms\n",
		nworks, nworks * rounds * ntimes, malloc_costtime + free_costtime);
}
int main()
{
	std::cout << "=========================================malloc=========================================" << std::endl;
	BenchmarkMalloc(10000, 4, 100);
	std::cout << "========================================================================================" << std::endl;
	std::cout << std::endl << std::endl;;
	std::cout << "========================================MyMalloc========================================" << std::endl;
	BenchmarkMyMalloc(10000, 4, 100);
	std::cout << "========================================================================================" << std::endl;
	return 0;
}