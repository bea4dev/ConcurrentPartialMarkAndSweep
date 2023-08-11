#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stack>

#include "heap_object.hpp"
#include "spin_lock.hpp"


extern SpinLock list_lock;
extern unordered_set<HeapObject*> suspected_object_set;


inline void add_suspected_object(HeapObject* object) {
    list_lock.lock();
    suspected_object_set.insert(object);
    list_lock.unlock();
}


void gc_collect();



/**
 * 循環参照のルートオブジェクトとなり得るかどうかをチェックして登録する
 */
inline void try_add_suspected_object(HeapObject* object, size_t previous_ref_count) {
    if (previous_ref_count == 1 && object->is_cyclic_type) {
        auto expected = false;
        if (object->buffered.compare_exchange_strong(expected, true, memory_order_relaxed)) {
            add_suspected_object(object);
        }
    }
}


/**
 * 循環参照コレクタに監視されているオブジェクトに対する解放処理
 * 解放可能であることをマークしておき、後ほどコレクタに解放させる。
 */
inline void drop_object_for_cyclic_type(HeapObject* object) {
    //各フィールドのオブジェクトの参照カウントを一つ減らし、0になればこの関数を再帰的に呼び出す
    object->lock();
    auto** fields = (HeapObject**) (object + 1);
    size_t field_length = object->field_length;
    for (size_t i = 0; i < field_length; i++) {
        auto* field_object = fields[i];
        if (field_object != nullptr) {
            //参照カウントを一つ減らす
            auto previous_ref_count = ((atomic_size_t*) &field_object->reference_count)->fetch_sub(1, memory_order_release);

            if (previous_ref_count == 1) {
                //他のスレッドでの変更を取得
                atomic_thread_fence(memory_order_acquire);
                
                //解放処理の重複を防ぐため、参照を切っておく
                if (field_object->is_cyclic_type && field_object->buffered.load(memory_order_acquire)) {
                    fields[i] = nullptr;
                }

                //再帰的に呼び出し
                drop_object_for_cyclic_type(field_object);
            } else {
                //解放処理の重複を防ぐため、参照を切っておく
                fields[i] = nullptr;
            }
        }
    }

    object->unlock();
    //解放可能としてマーク
    object->ready_to_release_with_gc.store(true, memory_order_release);
}