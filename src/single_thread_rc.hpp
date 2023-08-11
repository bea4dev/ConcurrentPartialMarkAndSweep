#pragma once

#include "heap_object.hpp"


/**
 * シングルスレッド専用参照カウント
 */
class SingleThreadRC {

private:
    //オブジェクト本体へのポインタ
    HeapObject* object_ref;

public:
    inline explicit SingleThreadRC(HeapObject* object_ref) {
        this->object_ref = object_ref;
    }

    /**
     * コピーコンストラクタ
     * コピー時に参照カウントを一つ増やす
     */
    inline SingleThreadRC(const SingleThreadRC& rc) {
        auto* object_ref = rc.object_ref;
        object_ref->reference_count++;
        this->object_ref = object_ref;
    }

    /**
     * デストラクタ
     * 呼び出される度に参照カウントを一つ減らす
     */
    inline ~SingleThreadRC() {
        //参照カウントを一つ減らす
        size_t previous_ref_count = this->object_ref->reference_count--;

        //減らした結果が0であれば削除処理を実行
        if (previous_ref_count == 1) {
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
                    SingleThreadRC rc(field_object);
                }
            }

            free(this->object_ref);


            #if RC_VALIDATION
                //生存しているオブジェクト数を一つ減らす
                global_object_count.fetch_sub(1, memory_order_relaxed);
            #endif
        }
    }


    /**
     * 指定された番号のフィールドにオブジェクト若くは nullptr を挿入
     */
    inline void set_object(size_t field_index, optional<SingleThreadRC> rc) {
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
            object->reference_count++;
        }
        
        //フィールド内へ既に挿入されているオブジェクトを取得
        auto* field_old_object = *field_ptr;
        //フィールドへ挿入
        *field_ptr = object;

        if (field_old_object != nullptr) {
            //デストラクタを呼び出し、既に挿入されていたオブジェクトの参照カウントを一つ減らす
            SingleThreadRC rc(field_old_object);
        }
    }


    /**
     * 指定された番号のフィールドにあるオブジェクトを取得
     */
    inline optional<SingleThreadRC> get_object(size_t field_index) {
        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);
        //対象となるフィールドのポインタ
        auto** field_ptr = field_start_ptr + field_index;

        //フィールドからロード
        auto* field_object = *field_ptr;
        if (field_object != nullptr) {
            //参照カウントを一つ増やす
            field_object->reference_count++;
        }

        if (field_object == nullptr) {
            return nullopt;
        } else {
            return SingleThreadRC(field_object);
        }
    }

};

