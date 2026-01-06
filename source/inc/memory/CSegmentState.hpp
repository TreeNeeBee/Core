// Copyright (c) 2025 LightAP Project. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef LIGHTAP_CORE_MEMORY_CSEGMENTSTATE_HPP
#define LIGHTAP_CORE_MEMORY_CSEGMENTSTATE_HPP

#include "CTypedef.hpp"
#include <atomic>
#include <vector>

namespace lap::core {

/**
 * @brief Segment state管理类（对标 iceoryx2/segment_state.rs）
 * 
 * 职责：
 * 1. 跟踪每个样本的订阅者引用计数（sample_reference_counter）
 * 2. 管理样本的payload大小信息
 * 3. 提供原子的borrow/release操作
 * 
 * 关键设计：
 * - sample_reference_counter_[i] 跟踪第i个样本被多少Subscriber持有
 * - 与 ChunkHeader.ref_count 的区别：这里只计数Subscriber引用，不包括Publisher
 * - borrowSample/releaseSample 返回旧值，用于检测首次/最后引用
 */
class CSegmentState {
public:
    /**
     * @brief 构造函数
     * @param number_of_samples 此segment中的样本总数
     */
    explicit CSegmentState(UInt32 number_of_samples);
    
    /**
     * @brief 析构函数 - 释放样本引用计数器数组
     */
    ~CSegmentState();
    
    /**
     * @brief 设置样本的payload大小
     * @param size 样本大小（字节）
     * 
     * 对标 iceoryx2: segment_state.rs:set_payload_size()
     */
    void setPayloadSize(UInt32 size);
    
    /**
     * @brief 获取样本的payload大小
     * @return 样本大小（字节）
     * 
     * 对标 iceoryx2: segment_state.rs:payload_size()
     */
    UInt32 payloadSize() const;
    
    /**
     * @brief 根据chunk偏移计算样本索引
     * @param distance_to_chunk 从segment起始地址到chunk的偏移（字节）
     * @return 样本索引（0-based）
     * 
     * 对标 iceoryx2: segment_state.rs:sample_index()
     * 
     * 计算公式：sample_index = distance_to_chunk / payload_size
     */
    UInt32 sampleIndex(UInt32 distance_to_chunk) const;
    
    /**
     * @brief Subscriber借用样本（增加引用计数）
     * @param distance_to_chunk 从segment起始地址到chunk的偏移（字节）
     * @return 增加前的引用计数（旧值）
     * 
     * 对标 iceoryx2: segment_state.rs:borrow_sample()
     * 
     * 原子操作：sample_reference_counter[i].fetch_add(1, Ordering::Relaxed)
     * 返回旧值用于检测首次引用（old_value == 0 表示第一个Subscriber）
     */
    UInt64 borrowSample(UInt32 distance_to_chunk);
    
    /**
     * @brief Subscriber释放样本（减少引用计数）
     * @param distance_to_chunk 从segment起始地址到chunk的偏移（字节）
     * @return 减少前的引用计数（旧值）
     * 
     * 对标 iceoryx2: segment_state.rs:release_sample()
     * 
     * 原子操作：sample_reference_counter[i].fetch_sub(1, Ordering::Relaxed)
     * 返回旧值用于检测最后引用（old_value == 1 表示最后一个Subscriber，应归还pool）
     */
    UInt64 releaseSample(UInt32 distance_to_chunk);
    
    /**
     * @brief 获取样本当前引用计数（调试用）
     * @param distance_to_chunk 从segment起始地址到chunk的偏移（字节）
     * @return 当前引用计数
     */
    UInt64 getReferenceCount(UInt32 distance_to_chunk) const;
    
    /**
     * @brief 获取样本总数
     * @return 样本总数
     */
    UInt32 numberOfSamples() const { return number_of_samples_; }
    
    /**
     * @brief 扩展样本数组以支持更多样本（动态segment分配）
     * @param new_total_samples 新的样本总数
     * @return true if successful, false on allocation failure
     * 
     * 注意：此函数不是线程安全的，应在allocateNewSegment()持有mutex时调用
     */
    bool expandCapacity(UInt32 new_total_samples);

private:
    /// 样本引用计数数组（每个样本独立计数）
    /// 对标 iceoryx2: sample_reference_counter: Vec<AtomicU64>
    /// 使用原始指针因为std::atomic不可复制/移动
    std::atomic<UInt64>* sample_reference_counter_;
    
    /// 样本的payload大小（用于计算sample_index）
    /// 对标 iceoryx2: payload_size: AtomicUsize
    std::atomic<UInt32> payload_size_;
    
    /// 样本总数
    UInt32 number_of_samples_;
};

} // namespace lap::core

#endif // LIGHTAP_CORE_MEMORY_CSEGMENTSTATE_HPP
