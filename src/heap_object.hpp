#pragma once

//参照カウントアルゴリズムの妥当性検証に使用するかどうか
//true に設定するとオブジェクトの作成時と破棄時にカウンタを増減させて、正しく動作しているかどうかを確かめることができる
#define RC_VALIDATION false

#include <cstddef>
#include <atomic>
#include <optional>
#include <iostream>
#include <unordered_set>
#include <vector>

using namespace std;


#if RC_VALIDATION
    extern atomic_size_t global_object_count;
#endif


/**
 * オブジェクトのヘッダ部分
 */
class HeapObject {

public:
    //参照カウント
    size_t reference_count;
    //フィールドの長さ
    size_t field_length;
    //このオブジェクトが複数のスレッドからアクセスされる可能性があるかどうか
    //詳細は"dynamic_rc_hpp"を参照
    bool is_mutex;
    //スピンロックに使用するためのフラグ
    atomic_flag spin_lock_flag;

    // >>> 循環参照コレクタ用付加情報
    //このオブジェクトが循環性のある型かどうか
    bool is_cyclic_type;
    //CycleCollectorで回収するかどうか
    atomic_bool ready_to_release_with_gc;
    //循環参照のルートオブジェクトとして記録されているかどうか
    atomic_bool buffered;


    /**
     * このオブジェクト以下のオブジェクト(フィールドに間接的に連なる全てのオブジェクトを含む)の is_mutex を true に伝搬させる
     * 詳細は"dynamic_rc_hpp"を参照
     */
    inline void to_mutex() {
        //is_mutex が false である場合
        if (!this->is_mutex) {
            this->is_mutex = true;

            auto field_length = this->field_length;
            //フィールドの開始ポインタ
            auto** field_start_ptr = (HeapObject**) (this + 1);

            for (size_t field_index = 0; field_index < field_length; field_index++) {
                //対象となるフィールドのポインタ
                auto** field_ptr = field_start_ptr + field_index;
                //フィールドの内容をロード
                auto* field_object = *field_ptr;

                if (field_object != nullptr) {
                    //再帰的に呼び出し
                    field_object->to_mutex();
                }
            }
        }
    }


    /**
     * spin_lock_flag を使用してスピンロック(lock)
     */
    inline void lock() {
        while (this->spin_lock_flag.test_and_set(memory_order_acquire)) {
            //spin
        }
    }

    /**
     * spin_lock_flag を使用してスピンロック(unlock)
     */
    inline void unlock() {
        this->spin_lock_flag.clear(memory_order_release);
    }

    inline void print_inner(unordered_set<HeapObject*>& objects) {
        if (objects.find(this) != objects.end()) {
            return;
        }

        objects.insert(this);

        size_t ref_count;
        if (this->is_mutex) {
            ref_count = ((atomic_size_t*) &this->reference_count)->load(memory_order_relaxed);
        } else {
            ref_count = this->reference_count;
        }

        cout << this << " | ref_count : " << ref_count << " | ";
        
        auto** fields = (HeapObject**) (this + 1);
        auto field_length = this->field_length;
        vector<HeapObject*> field_objects;

        for (size_t i = 0; i < field_length; i++) {
            auto* field_object = fields[i];
            if (field_object != nullptr) {
                field_objects.push_back(field_object);
                cout << field_object << " ";
            }
        }

        cout << endl;

        for (auto& object : field_objects) {
            object->print_inner(objects);
        }
    }

    inline void print() {
        unordered_set<HeapObject*> objects;
        this->print_inner(objects);
    }
};


/**
 * オブジェクトをヒープ領域に割り当て
 */
inline HeapObject* alloc_heap_object(size_t field_length) {
    //確保するサイズ
    //HeapObject をヘッダとしてそれに連なる形でフィールドの領域も合わせて確保
    auto allocate_size = sizeof(HeapObject) + sizeof(HeapObject*) * field_length;
    auto* object_ptr = (HeapObject*) malloc(allocate_size);
    
    //各フィールドを初期化
    //フィールドの開始ポインタ
    auto** field_start_ptr = (HeapObject**) (object_ptr + 1);
    for (size_t i = 0; i < field_length; i++) {
        *(field_start_ptr + i) = nullptr;
    }

    //ヘッダの各フィールドを初期化
    object_ptr->is_mutex = false;
    object_ptr->reference_count = 1;
    object_ptr->field_length = field_length;
    object_ptr->spin_lock_flag.clear();
    object_ptr->is_cyclic_type = false;
    object_ptr->ready_to_release_with_gc.store(false, memory_order_relaxed);
    object_ptr->buffered.store(false, memory_order_relaxed);
    //((atomic_size_t*) &object_ptr->reference_count)->store(1, memory_order_release);

    #if RC_VALIDATION
        //生存しているオブジェクト数を一つ増やす
        global_object_count.fetch_add(1, memory_order_relaxed);
    #endif

    return object_ptr;
}