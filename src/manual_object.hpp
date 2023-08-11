#pragma once

#include "heap_object.hpp"


/**
 * 手動メモリ管理を行うためのラッパークラス
 */
class ManualObject {

private:
    //オブジェクト本体へのポインタ
    HeapObject* object_ref;

public:
    inline explicit ManualObject(HeapObject* object_ref) {
        this->object_ref = object_ref;
    }

    /**
     * 指定された番号のフィールドにオブジェクト若くは nullptr を挿入
     */
    inline void set_object(size_t field_index, optional<ManualObject> manual_object) {
        //manual_object が nullopt であれば nullptr
        //そうでなければオブジェクトへのポインタを取得
        HeapObject* object = nullptr;
        if (manual_object.has_value()) {
            object = manual_object.value().object_ref;
        }

        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);
        //対象となるフィールドのポインタ
        auto** field_ptr = field_start_ptr + field_index;
        //フィールドへ挿入
        *field_ptr = object;
    }

    /**
     * 指定された番号のフィールドにあるオブジェクトを取得
     */
    inline optional<ManualObject> get_object(size_t field_index) {
        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);
        //対象となるフィールドのポインタ
        auto** field_ptr = field_start_ptr + field_index;

        //フィールドからロード
        auto* field_object = *field_ptr;

        if (field_object == nullptr) {
            return nullopt;
        } else {
            return ManualObject(field_object);
        }
    }

    /**
     * このオブジェクトとそのフィールドのオブジェクトを再帰的に削除
     */
    inline void detele_object() {
        auto field_length = this->object_ref->field_length;
        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);

        //フィールドに格納されている全オブジェクトを削除
        for (size_t field_index = 0; field_index < field_length; field_index++) {
            auto** field_ptr = field_start_ptr + field_index;
            auto* field_object = *field_ptr;

            if (field_object != nullptr) {
                ManualObject manual_object(field_object);
                manual_object.detele_object();
            }
        }

        free(this->object_ref);


        #if RC_VALIDATION
            //生存しているオブジェクト数を一つ減らす
            global_object_count.fetch_sub(1, memory_order_relaxed);
        #endif
    }

};

