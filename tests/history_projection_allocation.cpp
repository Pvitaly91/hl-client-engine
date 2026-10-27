#include "history_projection_allocation.hpp"
#include <cstdlib>
#include <new>
#include <malloc.h>
namespace {
thread_local bool counting=false;
thread_local history_allocation::Count count;
void account(std::size_t n) noexcept { if(counting) {++count.calls;count.bytes+=n;} }
}
namespace history_allocation {
void start() noexcept { count={};counting=true; }
Count stop() noexcept { counting=false;return count; }
}
void* operator new(std::size_t n) { if(auto p=std::malloc(n?n:1)) {account(n);return p;}throw std::bad_alloc{}; }
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t n,std::align_val_t a) {
  if(auto p=_aligned_malloc(n?n:1,static_cast<std::size_t>(a))) {account(n);return p;}throw std::bad_alloc{};
}
void* operator new[](std::size_t n,std::align_val_t a) {return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {_aligned_free(p);}
