#pragma once

#include <assert.h>

#include <memory>

struct ReleaseDeleter {
    template <typename T>
    void operator()(T* ptr)
    {
        ptr->Release();
    }
};

template <typename T>
struct ReleasePtr : std::unique_ptr<T, ReleaseDeleter> {
    using std::unique_ptr<T, ReleaseDeleter>::unique_ptr;

    T** operator&()
    {
        assert(!*this);
        return reinterpret_cast<T**>(this);
    }

    T* const* operator&() const
    {
        return reinterpret_cast<T* const*>(this);
    }

    operator T* () const
    {
        return this->get();
    }
};



#define CHECK_HR(Operation)                                          \
    if (FAILED(hr)) {                                                \
        std::cerr << "Error in " #Operation ": " << hr << std::endl; \
        return -1;                                                   \
    }
