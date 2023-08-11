#pragma once

#include "heap_object.hpp"


/**
 * スレッドセーフな即時参照カウント
 * 
 * 基本的には『ガベージコレクション 自動的メモリ管理を構成する理論と実装』の18章「並行参照カウント法」にて取り上げられている
 * ロックを用いた単純な並行即時参照カウント法を参考に実装した。
 * ロックは単純なスピンロックを実装した。
 * 加えて、カウンタの増減時のメモリバリアについては以下も参考にした。
 *  + https://github.com/rust-lang/rust/blob/master/library/alloc/src/sync.rs
 *  + https://www.boost.org/doc/libs/1_55_0/doc/html/atomic/usage_examples.html
 */
class ThreadSafeRC {

private:
    //オブジェクト本体へのポインタ
    HeapObject* object_ref;

public:
    inline explicit ThreadSafeRC(HeapObject* object_ref) {
        this->object_ref = object_ref;
    }

    /**
     * コピーコンストラクタ
     * コピー時に参照カウントを一つ増やす
     */
    inline ThreadSafeRC(const ThreadSafeRC& rc) {
        auto* object_ref = rc.object_ref;
        //atomic_size_t として参照カウントを一つ増やす
        //オブジェクト作成時の参照カウントの設定は atomic_size_t で行っていないが、恐らく上手く動作する(?)
        //少なくとも AArch64 では上手く動作しているように見える
        ((atomic_size_t*) &object_ref->reference_count)->fetch_add(1, memory_order_relaxed);
        this->object_ref = object_ref;
    }

    /**
     * デストラクタ
     * 呼び出される度に参照カウントを一つ減らす
     */
    inline ~ThreadSafeRC() {
        //参照カウントを一つ減らす
        //安全性の詳細については以下を参照
        // + https://github.com/rust-lang/rust/blob/master/library/alloc/src/sync.rs
        // + https://www.boost.org/doc/libs/1_55_0/doc/html/atomic/usage_examples.html
        size_t previous_ref_count = ((atomic_size_t*) &this->object_ref->reference_count)->fetch_sub(1, memory_order_release);
        if (previous_ref_count != 1) {
            //減らした後の参照カウントが0でない場合は何もしない
            return;
        }

        //他のスレッドでの変更を取得
        atomic_thread_fence(memory_order_acquire);

        auto field_length = this->object_ref->field_length;
        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);

        //フィールドに格納されている全オブジェクトの参照カウントを一つ減らす
        for (size_t field_index = 0; field_index < field_length; field_index++) {
            //対象となるフィールドのポインタ
            auto** field_ptr = field_start_ptr + field_index;
            //フィールドの内容をロード
            auto* field_object = *field_ptr;

            if (field_object != nullptr) {
                //デストラクタを呼び出し、参照カウントを一つ減らす
                ThreadSafeRC rc(field_object);
            }
        }

        free(this->object_ref);


        #if RC_VALIDATION
            //生存しているオブジェクト数を一つ減らす
            global_object_count.fetch_sub(1, memory_order_relaxed);
        #endif
    }


    /**
     * オブジェクトの spin_lock_flag を使用してスピンロック(lock)
     */
    inline void lock() {
        while (this->object_ref->spin_lock_flag.test_and_set(memory_order_acquire)) {
            //spin
        }
    }

    /**
     * オブジェクトの spin_lock_flag を使用してスピンロック(unlock)
     */
    inline void unlock() {
        this->object_ref->spin_lock_flag.clear(memory_order_release);
    }



    /**
     * 指定された番号のフィールドにオブジェクト若くは nullptr を挿入
     */
    inline void set_object(size_t field_index, optional<ThreadSafeRC> rc) {
        //rc が nullopt であれば nullptr
        //そうでなければオブジェクトへのポインタを取得
        HeapObject* object = nullptr;
        if (rc.has_value()) {
            object = rc.value().object_ref;
        }

        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);
        //対象となるフィールドのポインタ
        auto** field_ptr = field_start_ptr + field_index;

        if (object != nullptr) {
            //参照カウントを一つ増やす
            ((atomic_size_t*) &object->reference_count)->fetch_add(1, memory_order_relaxed);
        }
        
        //スピンロックを使用してフィールドのオブジェクトを不可分的に入れ替える
        this->lock();
        auto* field_old_object = *field_ptr;
        *field_ptr = object;
        this->unlock();

        if (field_old_object != nullptr) {
            //デストラクタを呼び出し、既に挿入されていたオブジェクトの参照カウントを一つ減らす
            ThreadSafeRC rc(field_old_object);
        }
    }


    /**
     * 指定された番号のフィールドにあるオブジェクトを取得
     */
    inline optional<ThreadSafeRC> get_object(size_t field_index) {
        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);
        //対象となるフィールドのポインタ
        auto** field_ptr = field_start_ptr + field_index;

        //スピンロックを使用して以下の操作を不可分的に行う
        // 1. フィールドからロード
        // 2. ロードしたオブジェクトの参照カウントを一つ増やす
        this->lock();
        auto* field_object = *field_ptr;
        if (field_object != nullptr) {
            ((atomic_size_t*) &field_object->reference_count)->fetch_add(1, memory_order_relaxed);
        }
        this->unlock();

        if (field_object == nullptr) {
            return nullopt;
        } else {
            return ThreadSafeRC(field_object);
        }
    }

};
