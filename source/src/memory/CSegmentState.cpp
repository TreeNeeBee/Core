// Copyright (c) 2025 LightAP Project. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "memory/CSegmentState.hpp"
#include <cassert>

namespace lap::core {

CSegmentState::CSegmentState(UInt32 number_of_samples)
    : sample_reference_counter_(nullptr), payload_size_(0), number_of_samples_(number_of_samples) {
    // 初始化所有样本的引用计数为0
    // 对标 iceoryx2: Vec::with_capacity() + push(AtomicU64::new(0))
    // 使用原始数组因为std::atomic不可复制/移动
    sample_reference_counter_ = new std::atomic<UInt64>[number_of_samples];
    for (UInt32 i = 0; i < number_of_samples; ++i) {
        sample_reference_counter_[i].store(0, std::memory_order_relaxed);
    }
}

CSegmentState::~CSegmentState() {
    // 对标 iceoryx2: Drop trait (RAII)
    delete[] sample_reference_counter_;
}

void CSegmentState::setPayloadSize(UInt32 size) {
    // 对标 iceoryx2: self.payload_size.store(value, Ordering::Relaxed)
    payload_size_.store(size, std::memory_order_relaxed);
}

UInt32 CSegmentState::payloadSize() const {
    // 对标 iceoryx2: self.payload_size.load(Ordering::Relaxed)
    return payload_size_.load(std::memory_order_relaxed);
}

UInt32 CSegmentState::sampleIndex(UInt32 distance_to_chunk) const {
    // 在我们的实现中，distance_to_chunk已经是chunk index（sample index）
    // 不需要除以payload_size
    // 对标 iceoryx2: distance_to_chunk 本身就是sample index
    return distance_to_chunk;
}

UInt64 CSegmentState::borrowSample(UInt32 distance_to_chunk) {
    // 对标 iceoryx2: 
    // self.sample_reference_counter[self.sample_index(distance_to_chunk)]
    //     .fetch_add(1, Ordering::Relaxed)
    UInt32 index = sampleIndex(distance_to_chunk);
    assert(index < number_of_samples_ && "Sample index out of bounds");
    
    return sample_reference_counter_[index].fetch_add(1, std::memory_order_relaxed);
}

UInt64 CSegmentState::releaseSample(UInt32 distance_to_chunk) {
    // 对标 iceoryx2:
    // self.sample_reference_counter[self.sample_index(distance_to_chunk)]
    //     .fetch_sub(1, Ordering::Relaxed)
    UInt32 index = sampleIndex(distance_to_chunk);
    assert(index < number_of_samples_ && "Sample index out of bounds");
    
    return sample_reference_counter_[index].fetch_sub(1, std::memory_order_relaxed);
}

UInt64 CSegmentState::getReferenceCount(UInt32 distance_to_chunk) const {
    UInt32 index = sampleIndex(distance_to_chunk);
    assert(index < number_of_samples_ && "Sample index out of bounds");
    
    return sample_reference_counter_[index].load(std::memory_order_relaxed);
}

bool CSegmentState::expandCapacity(UInt32 new_total_samples) {
    if (new_total_samples <= number_of_samples_) {
        return true;  // No expansion needed
    }
    
    // Allocate new larger array
    std::atomic<UInt64>* new_counters = new (std::nothrow) std::atomic<UInt64>[new_total_samples];
    if (!new_counters) {
        return false;  // Allocation failed
    }
    
    // Copy existing counters (atomic load/store)
    for (UInt32 i = 0; i < number_of_samples_; ++i) {
        new_counters[i].store(
            sample_reference_counter_[i].load(std::memory_order_relaxed),
            std::memory_order_relaxed
        );
    }
    
    // Initialize new counters to 0
    for (UInt32 i = number_of_samples_; i < new_total_samples; ++i) {
        new_counters[i].store(0, std::memory_order_relaxed);
    }
    
    // Swap and delete old array
    std::atomic<UInt64>* old_counters = sample_reference_counter_;
    sample_reference_counter_ = new_counters;
    number_of_samples_ = new_total_samples;
    
    delete[] old_counters;
    
    return true;
}

} // namespace lap::core
